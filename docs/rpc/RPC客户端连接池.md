# RPC 客户端连接池

## 1. RPC 客户端连接池概述

+ 对 `RpcClient` 更高级的封装
+ 提供连接池、服务地址缓存和负载均衡功能
+ 提供三种调用方式：同步调用、半同步调用、异步回调

## 2. RPC 连接池流程

+ (1) 创建连接池对象
+ (2) 绑定服务注册中心地址
+ (3) 发起远程过程调用或订阅服务。

## 3. RPC 连接池设计

### 3.1 代码框架

```C++
// RPC客户端连接池
class RpcConnectionPool : public std::enable_shared_from_this<RpcConnectionPool>
{
public:
    using ptr = std::shared_ptr<RpcConnectionPool>;
    using MutexType = CoMutex;

    RpcConnectionPool(uint64_t timeout_ms = -1);
    ~RpcConnectionPool();

    // 连接 RPC 服务中心
    bool connect(Address::ptr address);

    void close();

    // 异步远程过程调用回调模式
    template <typename... Params>
    void callback(const std::string &name, Params &&...ps)；

    // 异步远程过程调用
    template <typename R, typename... Params>
    Channel<Result<R>> async_call(const std::string &name, Params &&...ps)；

    // 远程过程调用
    template <typename R, typename... Params>
    Result<R> call(const std::string &name, Params... ps)；

    // 订阅消息
    template <typename Func>
    void subscribe(const std::string &key, Func func)；


private:
    // 服务发现
    std::vector<std::string> discover(const std::string &name);

    // rpc 连接对象的发送协程，通过 Channel 收集调用请求，并转发请求给注册中心。
    void handleSend();

    // rpc 连接对象的接收协程，负责接收注册中心发送的 response 响应并根据响应类型进行处理
    void handleRecv();

    // 处理注册中心服务发现响应
    void handleServiceDiscover(Protocol::ptr response);

    // 处理发布消息
    void handlePublish(Protocol::ptr proto);

private:
    bool m_isClose = true;
    bool m_isHeartClose = true;
    uint64_t m_timeout;

    std::map<std::string, std::vector<std::string>> m_serviceCache; // 服务名到全部缓存的服务地址列表映射

    std::map<std::string, RpcClient::ptr> m_conns; // 服务名和服务地址的连接池
    MutexType m_connMutex;                         // 保护 m_conns

    RpcSession::ptr m_registry;    // 服务中心连接
    Timer::ptr m_heartTimer;       // 服务中心心跳定时器
    Channel<Protocol::ptr> m_chan; // 注册中心消息发送通道

    std::map<std::string, Channel<Protocol::ptr>> m_discover_handle; // 服务名到对应调用者协程的 Channel 映射
    MutexType m_discover_mutex;                                      // m_discover_handle 的 mutex

    std::map<std::string, std::function<void(Serializer)>> m_subHandle; // 处理订阅的消息回调函数
    MutexType m_sub_mtx;                                                // 保护m_subHandle
};
```

### 3.2 重要成员变量

+ **`m_conns`** ：服务名和服务地址的连接池，发起调用时，首先从连接池中取出连接执行 call() 调用，若还未建立连接，则建立连接，并加入连接池。
+ **`m_registry`** ：与服务注册中心的会话。
+ **`m_serviceCache`** ：服务名到全部缓存的服务地址列表映射，发起调用时，先从缓存地址中查询本次调用服务的服务地址列表，
  + 若查询到服务地址列表，执行负载均衡策略，选择一个服务地址执行调用；
  + 若未查询到，再次执行服务发现，将发现到的本服务的服务地址列表缓存到 `m_serviceCache`，执行负载均衡策略，选择一个服务地址执行调用；
+ **`m_subHandle`** ：处理订阅的消息回调函数，每订阅一个服务，就将回调函数保存起来，订阅的服务发布后，从中找到回调函数执行。
+ **`m_discover_handle`** ：服务名到对应服务发现调用者协程消息通道的 `Channel` 映射，若接收端受到服务注册中心的 `RPC_SERVICE_DISCOVER_RESPONSE` 响应，但此时调用此服务的协程不存在了，就直接返回，若还存在，就接收服务注册响应，并唤醒调用者协程。

### 3.3 重要成员方法

#### 3.3.1 `connect()` 方法

+ 连接**服务注册中心**，并开启 **`send`** 和 **`recv`** 两个协程处理消息的收发。
+ 健康检测，自动发送心跳包。

    ```C++
    bool RpcConnectionPool::connect(Address::ptr address)
    {
        Socket::ptr sock = Socket::CreateTCP(address);
        if (!sock)
        {
            return false;
        }
        if (!sock->connect(address, m_timeout))
        {
            DBSPIDER_LOG_ERROR(g_logger) << "connect to register fail";
            m_registry = nullptr;
            return false;
        }
        m_registry = std::make_shared<RpcSession>(sock);

        DBSPIDER_LOG_DEBUG(g_logger) << "connect to registry: " << m_registry->getSocket()->toString();

        go[this]
        {
            // 开启 recv 协程
            handleRecv();
        };

        go[this]
        {
            // 开启 send 协程
            handleSend();
        };

        // 服务中心心跳定时器 30s
        m_heartTimer = dbspider::IOManager::GetThis()->addTimer(
            30'000, [this]
            {
                DBSPIDER_LOG_DEBUG(g_logger) << "heart beat";
                if (m_isHeartClose)
                {
                    DBSPIDER_LOG_DEBUG(g_logger) << "registry closed";
                    // 放弃服务中心
                    m_heartTimer->cancel();
                    m_heartTimer = nullptr;
                }
                // 创建心跳包
                Protocol::ptr proto = Protocol::Create(Protocol::MsgType::HEARTBEAT_PACKET, "");
                // 向 send 协程的 Channel 发送消息
                m_chan << proto;
                m_isHeartClose = true; },
            true);

        return true;
    }
    ```

##### 3.3.1.1 `send()` 方法

+ **send** 协程调用，用于和服务注册中心收发消息，通过消息发送通道 `m_chan`，只有 `m_chan` 有消息，就会向对方发送。
+ **send** 协程主要向对端发送以下消息，通过消息发送通道收集
  + 心跳包 `HEARTBEAT_PACKET`；
  + 服务发现响应 `RPC_SERVICE_DISCOVER`，向服务注册中心发送服务发现请求
  + 服务发布响应 `RPC_PUBLISH_RESPONSE`，接收到服务发布消息后，向对端发送响应。

    ```C++
    void RpcConnectionPool::handleSend()
    {
        Protocol::ptr request;
        // 通过 Channel 收集调用请求，如果没有消息时 Channel 内部会挂起该协程等待消息到达
        // Channel 被关闭时会退出循环
        while (m_chan >> request)
        {
            if (!request)
            {
                DBSPIDER_LOG_WARN(g_logger) << "RpcConnectionPool::handleSend() fail";
                continue;
            }
            // 发送请求
            m_registry->sendProtocol(request);
        }
    }
    ```

##### 3.3.1.2 `recv()` 方法

+ **recv** 协程调用，用于接收服务调用结果和服务发布消息。
  + 当接收到 `RPC_SERVICE_DISCOVER_RESPONSE` 服务发现响应时，调用 `handleServiceDiscover()` ，唤醒 `discover()` 调用协程。
  + 当接收到 `RPC_PUBLISH_REQUEST` 服务注册中心发布的消息时，调用 `handlePublish()``，处理订阅消息，并返回RPC_PUBLISH_RESPONSE` 响应。

    ```C++
    void RpcConnectionPool::handleRecv()
    {
        if (!m_registry->isConnected())
        {
            return;
        }
        while (true)
        {
            // 接收响应
            Protocol::ptr response = m_registry->recvProtocol();
            if (!response)
            {
                DBSPIDER_LOG_WARN(g_logger) << "RpcConnectionPool::handleRecv() fail";
                close();
                break;
            }
            m_isHeartClose = false;
            Protocol::MsgType type = response->getMsgType();
            // 判断响应类型进行对应的处理
            switch (type)
            {
            case Protocol::MsgType::HEARTBEAT_PACKET:
                m_isHeartClose = false;
                break;
            case Protocol::MsgType::RPC_SERVICE_DISCOVER_RESPONSE:
                handleServiceDiscover(response);
                break;
            case Protocol::MsgType::RPC_PUBLISH_REQUEST:
                handlePublish(response);
                m_chan << Protocol::Create(Protocol::MsgType::RPC_PUBLISH_RESPONSE, "");
                break;
            case Protocol::MsgType::RPC_SUBSCRIBE_RESPONSE:
                break;
            default:
                DBSPIDER_LOG_DEBUG(g_logger) << "protocol:" << response->toString();
                break;
            }
        }
    }
    ```

+ `handleServiceDiscover()` 方法，获取服务注册中心发来的服务发现结果，也就是服务地址列表，通过通道接收，唤醒 discover() 调用协程，继续处理。

    ```C++
    void RpcConnectionPool::handleServiceDiscover(Protocol::ptr response)
    {
        Serializer s(response->getContent());
        std::string service;
        s >> service;
        std::map<std::string, Channel<Protocol::ptr>>::iterator it;

        MutexType::Lock lock(m_discover_mutex);
        // 查找该服务名的 Channel 是否还存在，如果不存在直接返回
        it = m_discover_handle.find(service);
        if (it == m_discover_handle.end())
        {
            return;
        }
        // 通过服务名获取等待该结果的 Channel
        Channel<Protocol::ptr> chan = it->second;
        // 对该 Channel 发送调用结果唤醒调用者
        chan << response;
    }
    ```

+ `handlePublish()` 方法 ：从 `m_subHandle` 找到订阅服务对应的回调函数执行

    ```C++
    void RpcConnectionPool::handlePublish(Protocol::ptr proto)
    {
        Serializer s(proto->getContent());
        std::string key;
        s >> key;
        MutexType::Lock lock(m_sub_mtx);
        auto it = m_subHandle.find(key);
        if (it == m_subHandle.end())
        {
            return;
        }
        it->second(s);
    }
    ```

#### 3.3.2 `discover()` 方法

+ 向服务注册中心请求服务发现，根据服务名获取在服务中心注册的服务地址列表，并订阅服务变化信息。
  + 若有新的服务提供者节点加入，将服务地址加入服务列表缓存；
  + 若已有服务提供者节点下线，清理缓存中断开的连接地址；

```C++
std::vector<std::string> RpcConnectionPool::discover(const std::string &name)
{
    if (!m_registry || !m_registry->isConnected())
    {
        return {};
    }
    // 开启一个 Channel 接收调用结果
    Channel<Protocol::ptr> recvChan(1);

    std::map<std::string, Channel<Protocol::ptr>>::iterator it;
    {
        MutexType::Lock lock(m_discover_mutex);
        // 将请求服务名与接收 Channel 关联
        it = m_discover_handle.emplace(name, recvChan).first;
    }

    // 创建请求协议，附带上请求 id
    Protocol::ptr request = Protocol::Create(Protocol::MsgType::RPC_SERVICE_DISCOVER, name);

    // 向 send 协程的 Channel 发送消息
    m_chan << request;

    Protocol::ptr response = nullptr;

    // 等待 response，Channel内部会挂起协程，如果有消息到达或者被关闭则会被唤醒
    recvChan >> response;

    {
        MutexType::Lock lock(m_discover_mutex);
        m_discover_handle.erase(it);
    }

    if (!response)
    {
        return {};
    }

    std::vector<Result<std::string>> res;
    std::vector<std::string> rt;
    std::vector<Address::ptr> addrs;

    Serializer s(response->getContent());
    uint32_t cnt;
    std::string str;
    s >> str >> cnt;

    for (uint32_t i = 0; i < cnt; ++i)
    {
        Result<std::string> r;
        s >> r;
        res.push_back(r);
    }

    if (res.front().getCode() == RPC_NO_METHOD)
    {
        return {};
    }

    for (size_t i = 0; i < res.size(); ++i)
    {
        rt.push_back(res[i].getVal());
    }

    if (!m_subHandle.contains(RPC_SERVICE_SUBSCRIBE + name))
    {
        // 向注册中心订阅服务变化的消息
        subscribe(RPC_SERVICE_SUBSCRIBE + name,
                    [name, this](Serializer s)
                    {
                        // false 为服务下线，true 为新服务节点上线
                        bool isNewServer = false;
                        std::string addr;
                        s >> isNewServer >> addr;
                        MutexType::Lock lock(m_connMutex);
                        if (isNewServer)
                        {
                            // 一个新的服务提供者节点加入，将服务地址加入服务列表缓存
                            LOG_DEBUG << "service [ " << name << " : " << addr << " ] join";
                            m_serviceCache[name].push_back(addr);
                        }
                        else
                        {
                            // 已有服务提供者节点下线
                            LOG_DEBUG << "service [ " << name << " : " << addr << " ] quit";
                            // 清理缓存中断开的连接地址
                            auto its = m_serviceCache.find(name);
                            if (its != m_serviceCache.end())
                            {
                                std::erase(its->second, addr);
                            }
                        }
                    });
    }

    return rt;
}
```

#### 3.3.3 `call()` 方法

+ 三种调用方式
  + 1. 异步回调模式

    ```C++
    // 异步回调模式
    template <typename... Params>
    void callback(const std::string &name, Params &&...ps)
    {
        static_assert(sizeof...(ps), "without a callback function");

        // 1. 将参数打包为 tuple
        auto tp = std::make_tuple(ps...);
        constexpr auto size = std::tuple_size<typename std::decay<decltype(tp)>::type>::value;
        auto cb = std::get<size - 1>(tp);
        static_assert(function_traits<decltype(cb)>{}.arity == 1, "callback type not support");
        using res = typename function_traits<decltype(cb)>::template args<0>::type;
        using rt = typename res::row_type;
        static_assert(std::is_invocable_v<decltype(cb), Result<rt>>, "callback type not support");
        RpcConnectionPool::ptr self = shared_from_this();
        go[cb = std::move(cb), name = std::move(name), tp = std::move(tp), self, this]
        {
            auto proxy = [&cb, &name, &tp, this ]<std::size_t... Index>(std::index_sequence<Index...>)
            {
                cb(call<rt>(name, std::get<Index>(tp)...));
            };
            proxy(std::make_index_sequence<size - 1>{});
            (void)self;
        };
    }
    ```

    + 2. 半同步调用

    ```C++
    // 半同步调用
    template <typename R, typename... Params>
    Channel<Result<R>> async_call(const std::string &name, Params &&...ps)
    {
        Channel<Result<R>> chan(1);
        RpcConnectionPool::ptr self = shared_from_this();
        go[chan, name, ps..., self, this]() mutable
        {
            chan << call<R>(name, ps...);
            self = nullptr;
        };
        return chan;
    }
    ```

    + 3. 同步调用

    ```C++
    // 同步调用
    template <typename R, typename... Params>
    Result<R> call(const std::string &name, Params... ps)
    {
        MutexType::Lock lock(m_connMutex);
        // 从连接池里取出服务连接
        auto conn = m_conns.find(name);
        Result<R> result;
        if (conn != m_conns.end())
        {
            lock.unlock();
            result = conn->second->template call<R>(name, ps...);
            if (result.getCode() != RPC_CLOSED)
            {
                return result;
            }
            lock.lock();
            // 移除失效连接
            std::vector<std::string> &addrs = m_serviceCache[name];
            std::erase(addrs, conn->second->getSocket()->getRemoteAddress()->toString());

            m_conns.erase(name);
        }

        std::vector<std::string> &addrs = m_serviceCache[name];

        // 如果服务地址缓存为空则重新向服务中心请求服务发现
        if (addrs.empty())
        {
            if (!m_registry || !m_registry->isConnected())
            {
                Result<R> res;
                res.setCode(RPC_CLOSED);
                res.setMsg("registry closed");
                return res;
            }
            addrs = discover(name);
            // 如果没有发现服务返回错误
            if (addrs.empty())
            {
                Result<R> res;
                res.setCode(RPC_NO_METHOD);
                res.setMsg("no method:" + name);
                return res;
            }
        }

        // 选择客户端负载均衡策略，根据路由策略选择服务地址
        RouteStrategy<std::string>::ptr strategy =
            RouteEngine<std::string>::queryStrategy(Strategy::Random);

        if (addrs.size())
        {
            const std::string ip = strategy->select(addrs);
            Address::ptr address = Address::LookupAny(ip);
            // 选择的服务地址有效
            if (address)
            {
                RpcClient::ptr client = std::make_shared<RpcClient>();
                // 成功连接上服务器
                if (client->connect(address))
                {
                    m_conns.template emplace(name, client);
                    lock.unlock();
                    return client->template call<R>(name, ps...);
                }
            }
        }
        result.setCode(RPC_FAIL);
        result.setMsg("call fail");
        return result;
    }
    ```

#### 3.3.4 `subscirbes()` 方法

+ 向服务注册中心订阅消息

    ```C++
    // 订阅消息
    template <typename Func>
    void subscribe(const std::string &key, Func func)
    {
        {
            MutexType::Lock lock(m_sub_mtx);
            auto it = m_subHandle.find(key);
            if (it != m_subHandle.end())
            {
                DBSPIDER_ASSERT2(false, "duplicated subscribe");
            }

            m_subHandle.emplace(key, std::move(func));
        }
        Serializer s;
        s << key;
        s.reset();
        Protocol::ptr response = Protocol::Create(Protocol::MsgType::RPC_SUBSCRIBE_REQUEST, s.toString(), 0);
        m_chan << response;
    }
    ```
