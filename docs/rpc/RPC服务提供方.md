# RPC服务提供方

## 1. RPC服务端 `RpcServer` 概述

+ 继承自 **`TcpServer`** 。
+ 向服务注册中心 **`RpcRegister`** **注册服务** ----> `registerMethod()` ----> `registerService()` 。
+ 处理客户端**远程过程调用** ----> `handleMethodCall()` ----> `call()`  。
+ 处理客户端**订阅**请求 ----> `handleSubscribe()` 。
+ 采用心跳进行**健康检查**：
  + 处理服务注册中心的 `m_heartTimer` ；
  + 处理客户端的 `handleHeartbeatPacket()` 。

## 2. RPC服务端流程

+ (1) 创建 **RpcServer** 对象；
+ (2) `bind()` 绑定本地地址；
+ (3) `bindRegistry()` 绑定服务注册中心地址；
+ (4) 调用 `registerMethod()`注册本地服务或调用 `publish()` 发布消息；
+ (5) 调用 `start()`方法开启 `RpcServer` 服务端；
  + 向服务注册中心 `registerService` ；
  + 开启与服务注册中心的心跳检测 `m_heartTimer` ；
  + 开启协程定时清理订阅列表；
  + 开启监听并处理服务请求；
    + 与客户端的心跳检测 `handleHeartbeatPacket`
    + 客户端远程过程调用 `handleMethodCall`
    + 处理订阅请求 `handleSubscribe`

## 3. RPC服务端设计

### 3.1 代码框架

```C++
// RPC服务端
class RpcServer : public TcpServer
{
public:
    using ptr = std::shared_ptr<RpcServer>;
    using MutexType = CoMutex;

    RpcServer(IOManager *worker = IOManager::GetThis(),
                IOManager *accept_worker = IOManager::GetThis());

    ~RpcServer();

    bool bind(Address::ptr address) override;
    bool bindRegistry(Address::ptr address);

    bool start() override;

    // 注册函数
    template <typename Func>
    void registerMethod(const std::string &name, Func func);

    // 发布消息
    template <typename T>
    void publish(const std::string &key, T data);

protected:
    // 更新心跳定时器
    void update(Timer::ptr &heartTimer, Socket::ptr client);

    // 向服务注册中心发起注册
    void registerService(const std::string &name);

    // 调用服务端注册的函数，返回序列化完的结果
    Serializer call(const std::string &name, const std::string &arg);

    // 处理客户端连接
    void handleClient(Socket::ptr client) override;

    // 处理客户端过程调用请求
    Protocol::ptr handleMethodCall(Protocol::ptr proto);

    // 处理心跳包
    Protocol::ptr handleHeartbeatPacket(Protocol::ptr proto);

    // 处理订阅请求
    Protocol::ptr handleSubscribe(Protocol::ptr proto, RpcSession::ptr client);

protected:
    // 调用代理
    template <typename F>
    void proxy(F fun, Serializer serializer, const std::string &arg);

private:
    std::map<std::string, std::function<void(Serializer, const std::string &)>> m_handlers; // 保存注册的函数
    RpcSession::ptr m_registry;                                                             // 服务中心连接
    Timer::ptr m_heartTimer;                                                                // 服务中心心跳定时器
    uint32_t m_port;                                                                        // 开放服务端口
    uint64_t m_AliveTime;                                                                   // 和客户端的心跳时间 默认 40s
    std::unordered_multimap<std::string, std::weak_ptr<RpcSession>> m_subscribes;           // 订阅的客户端
    MutexType m_sub_mtx;                                                                    // 保护 m_subscribes
    bool m_stop_clean = false;                                                              // 停止清理订阅协程
    Channel<bool> m_clean_chan{1};                                                          // 等待清理协程停止
};
```

### 3.2 重要成员变量

+ **`m_handlers`** ：一个 `map` ，保存注册的函数， `key` 为服务名， `value` 为服务方法；
+ **`m_subscribes`** ： 一个 `map` ，保存订阅的客户端， `key` 为 服务名， `value` 为与客户端的会话。
+ **`m_heartTimer`** ：与注册中心的心跳定时器。
+ **`m_registry`** ： 与服务注册中心的会话。

### 3.3 重要成员方法

#### 3.3.1 `bind()` 方法

+ 与本地地址绑定的 **`bind()`**

    ```C++
    bool RpcServer::bind(Address::ptr address)
    {
        m_port = std::dynamic_pointer_cast<IPv4Address>(address)->getPort();
        return TcpServer::bind(address);
    }
    ```

+ 与服务注册中心地址绑定的 **`bindRegistry()`**
  + 创建与服务注册中心连接的 `sock` ；
  + `connect` 服务注册中心，建立会话。
  + 向服务注册中心发送 `RPC_PROVIDER` 请求，声明为 `provider` 并注册服务端口 <===> 服务注册中心收到 `RPC_PROVIDER` 请求，调用 `handleProvider()` ，生成 `providerAddr` ；

    ```C++
    bool RpcServer::bindRegistry(Address::ptr address)
    {
        Socket::ptr sock = Socket::CreateTCP(address);

        if (!sock)
        {
            return false;
        }
        if (!sock->connect(address))
        {
            DBSPIDER_LOG_WARN(g_logger) << "can't connect to registry";
            m_registry = nullptr;
            return false;
        }
        m_registry = std::make_shared<RpcSession>(sock);

        Serializer s;
        s << m_port;
        s.reset();

        // 向服务中心声明为provider，注册服务端口
        Protocol::ptr proto = Protocol::Create(Protocol::MsgType::RPC_PROVIDER, s.toString());
        m_registry->sendProtocol(proto);
        return true;
    }
    ```

#### 3.3.2 `start()` 方法

+ 调用 `start()`方法开启 `RpcServer` 服务；
  + 1. 若已绑定服务注册中心地址，则调用 `registerService()` 方法，向服务注册中心发送 `RPC_SERVICE_REGISTER` 请求，注册所有服务 <====> 服务注册中心收到 `RPC_SERVICE_REGISTER` 请求，调用 `handleRegisterService()` 将服务名和服务地址保存到 `m_services` 中。
  + 2. 开启与服务注册中心的心跳定时器，定时（30s）发送心跳包 `HEARTBEAT_PACKET`
    + 若未超时，服务注册中心刷新定时器；
    + 若超时，则会被服务注册中心取消注册，并向订阅此服务的客户端发布服务下线通知。
  + 3. 开启协程定时清理订阅列表
  + 4. 开启监听，并处理客户端连接事件 --> `handleClient()`
 
    ```C++
    bool RpcServer::start()
    {
        if (m_registry)
        {
            for (auto &item : m_handlers)
            {
                registerService(item.first);
            }
            auto server = std::dynamic_pointer_cast<RpcServer>(shared_from_this());

            // 服务中心心跳定时器 30s
            m_registry->getSocket()->setRecvTimeout(30'000);
            m_heartTimer = m_worker->addTimer(
                30'000, [server]
                {
                    DBSPIDER_LOG_DEBUG(g_logger) << "heart beat";
                    Protocol::ptr proto = Protocol::Create(Protocol::MsgType::HEARTBEAT_PACKET, "");
                    server->m_registry->sendProtocol(proto);
                    Protocol::ptr response = server->m_registry->recvProtocol();

                    if (!response) 
                    {
                        DBSPIDER_LOG_WARN(g_logger) << "Registry close";
                        //server->stop();
                        //放弃服务中心，独自提供服务
                        server->m_heartTimer->cancel();
                    } },
                true);
        }

        // 开启协程定时清理订阅列表
        Go
        {
            while (!m_stop_clean)
            {
                sleep(5);
                MutexType::Lock lock(m_sub_mtx);
                for (auto it = m_subscribes.cbegin(); it != m_subscribes.cend();)
                {
                    auto conn = it->second.lock();
                    if (conn == nullptr || !conn->isConnected())
                    {
                        it = m_subscribes.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            m_clean_chan << true;
        };

        return TcpServer::start();
    }
    ```

#### 3.3.3 `handleClient()` 方法

+ 处理客户端心跳检测 --> `handleHeartbeatPacket()`
+ 处理客户端方法调用 --> `handleMethodCall()`
+ 处理客户端订阅 --> `handleSubscribe()`

    ```C++
    void RpcServer::handleClient(Socket::ptr client)
    {
        DBSPIDER_LOG_DEBUG(g_logger) << "handleClient: " << client->toString();
        RpcSession::ptr session = std::make_shared<RpcSession>(client);

        Timer::ptr heartTimer;
        // 开启心跳定时器
        update(heartTimer, client);
        while (true)
        {
            Protocol::ptr request = session->recvProtocol();
            if (!request)
            {
                break;
            }
            // 更新定时器
            update(heartTimer, client);

            Protocol::ptr response;
            Protocol::MsgType type = request->getMsgType();
            switch (type)
            {
            case Protocol::MsgType::HEARTBEAT_PACKET:
                response = handleHeartbeatPacket(request);
                break;
            case Protocol::MsgType::RPC_METHOD_REQUEST:
                response = handleMethodCall(request);
                break;
            case Protocol::MsgType::RPC_SUBSCRIBE_REQUEST:
                response = handleSubscribe(request, session);
                break;
            case Protocol::MsgType::RPC_PUBLISH_RESPONSE:
                return;
            default:
                DBSPIDER_LOG_DEBUG(g_logger) << "protocol:" << request->toString();
                break;
            }

            if (response)
            {
                session->sendProtocol(response);
            }
        }
    }
    ```

#### 3.3.3.1 心跳检测：`handleHeartbeatPacket()` 方法

+ 接收到客户端的 `HEARTBEAT_PACKET` 心跳包，调用 `handleHeartbeatPacket()` 向客户端返回心跳包。

    ```C++
    Protocol::ptr RpcServer::handleHeartbeatPacket(Protocol::ptr p)
    {
        return Protocol::HeartBeat();
    }
    ```

#### 3.3.3.2 服务调用：`handleMethodCall()` 方法

+ 收到客户端的 `RPC_METHOD_REQUEST` ，调用 `handleMethodCall()`，处理客户端的服务调用。
  + (1) 解析请求服务名和参数
  + (2) 调用服务端本地方法(`m_handlers`)
  + (3) 向客户端返回调用结果

  ```C++
  Protocol::ptr RpcServer::handleMethodCall(Protocol::ptr p)
  {
      std::string func_name;
      Serializer request(p->getContent());
      request >> func_name;
      Serializer rt = call(func_name, request.toString());
      Protocol::ptr response = Protocol::Create(
          Protocol::MsgType::RPC_METHOD_RESPONSE, rt.toString(), p->getSequenceId());
      return response;
  }
  ```

#### 3.3.3.3 服务订阅：`handleSubscribe()` 方法

+ 收到客户端的订阅消息 `RPC_SUBSCRIBE_REQUEST` ，调用 `handleSubscribe()` 将订阅key和与客户端的会话保存到 `m_subscribes`。

    ```C++
    Protocol::ptr RpcServer::handleSubscribe(Protocol::ptr proto, RpcSession::ptr client)
    {
        MutexType::Lock lock(m_sub_mtx);
        std::string key;
        Serializer s(proto->getContent());
        s >> key;
        m_subscribes.emplace(key, std::weak_ptr<RpcSession>(client));
        Result<> res = Result<>::Success();
        s.reset();
        s << res;
        return Protocol::Create(Protocol::MsgType::RPC_SUBSCRIBE_RESPONSE, s.toString(), 0);
    }
    ```

### 3.3.4 消息发布：`publish()` 方法

+ 向每一个订阅了 `key` 的客户端，发布消息

```C++
// 发布消息
template <typename T>
void publish(const std::string &key, T data)
{
    {
        MutexType::Lock lock(m_sub_mtx);
        if (m_subscribes.empty())
        {
            return;
        }
    }
    Serializer s;
    s << key << data;
    s.reset();
    Protocol::ptr pub = Protocol::Create(Protocol::MsgType::RPC_PUBLISH_REQUEST, s.toString(), 0);
    MutexType::Lock lock(m_sub_mtx);
    auto range = m_subscribes.equal_range(key);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto conn = it->second.lock();
        if (conn == nullptr || !conn->isConnected())
        {
            continue;
        }
        conn->sendProtocol(pub);
    }
}
```