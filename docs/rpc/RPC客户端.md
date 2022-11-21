# RPC客户端

## 1. RPC客户端 `RpcClient` 概述

+ 向RPC服务端和服务注册中心连接 ----> `connect()` 。
+ 提供远程调用接口 `call()` 和 订阅接口 `subscribe()`
+ 提供四种调用接口
  + 有参数调用
  + 无参数调用
  + 异步回调
  + 异步回调，返回结果 `Channel`
+ RPC服务消费者并不直接使用 `RpcClient` 而是采用更高级的封装 `RpcConnectionPool`。
  + 提供连接池和服务地址缓存服务。
  
## 2. RPC客户端流程

+ (1) `connect` 连接服务注册中心或RPC服务端
  + 开启 **recv** 协程和 **send** 协程
    + **send** 协程主要向对端发送以下消息
      + 心跳包 `HEARTBEAT_PACKET`
      + 服务调用请求 `RPC_METHOD_REQUEST`
      + 服务订阅请求 `RPC_SUBSCRIBE_REQUEST`
      + 服务发布响应 `RPC_PUBLISH_RESPONSE`
    + **recv** 协程主要接收对端以下消息
      + 心跳包 `HEARTBEAT_PACKET`
      + 服务调用结果 `RPC_METHOD_RESPONSE`
      + 服务发布结果 `RPC_PUBLISH_REQUEST`
      + 服务订阅响应 `RPC_SUBSCRIBE_RESPONSE`
  + 定时发送心跳包
+ (2) 发起服务调用 `call()` 或服务订阅 `subscribes()`

## 3. RPC客户端设计

### 3.1 代码框架

```C++
class RpcClient : public std::enable_shared_from_this<RpcClient>
{
public:
    using ptr = std::shared_ptr<RpcClient>;
    using MutexType = CoMutex;

    RpcClient(bool auto_heartbeat = true);

    ~RpcClient();

    void close();

    // 连接一个RPC服务器
    bool connect(Address::ptr address);

    // 有参数的调用
    template <typename R, typename... Params>
    Result<R> call(const std::string &name, Params... ps);

    // 无参数的调用
    template <typename R>
    Result<R> call(const std::string &name);

    // 异步回调模式
    template <typename... Params>
    void callback(const std::string &name, Params &&...ps);

    // 异步调用，返回一个 Channel
    template <typename R, typename... Params>
    Channel<Result<R>> async_call(const std::string &name, Params &&...ps);

    // 订阅消息
    template <typename Func>
    void subscribe(const std::string &key, Func func);

private:
    // 连接对象的发送协程，通过 Channel 收集调用请求，并转发请求给服务器。
    void handleSend();

    // rpc 连接对象的接收协程，负责接收服务器发送的 response 响应并根据响应类型进行处理
    void handleRecv();

    // 通过序列号获取对应调用者的 Channel，将 response 放入 Channel 唤醒调用者
    void handleMethodResponse(Protocol::ptr response);

    // 处理发布消息
    void handlePublish(Protocol::ptr proto);

    // 实际调用 s:序列化完的请求
    template <typename R>
    Result<R> call(Serializer s);

private:
    bool m_auto_heartbeat = true; // 是否自动开启心跳包
    bool m_isClose = true;
    bool m_isHeartClose = true;
    uint64_t m_timeout = -1;                                            // 超时时间
    RpcSession::ptr m_session;                                          // 服务器的连接
    uint32_t m_sequenceId = 0;                                          // 序列号
    std::map<uint32_t, Channel<Protocol::ptr>> m_responseHandle;        // 序列号到对应调用者协程的 Channel 映射
    MutexType m_mutex;                                                  // m_responseHandle 的 mutex
    Channel<Protocol::ptr> m_chan;                                      // 消息发送通道
    Timer::ptr m_heartTimer;                                            // service provider心跳定时器
    std::map<std::string, std::function<void(Serializer)>> m_subHandle; // 处理订阅的消息回调函数
    MutexType m_sub_mtx;                                                // 保护m_subHandle
};
```

### 3.2 重要成员变量

+ **`m_session`** ： 保存与对端的会话
+ **`m_chan`** : 消息发送通道，send 协程不断从中取消息，向注册中心或服务端发送消息。
+ **`m_subHandle`** ：处理订阅的消息回调函数，每订阅一个服务，就将回调函数保存起来，订阅的服务发布后，从中找到回调函数执行。
+ **`m_responseHandle`** ： 将请求序列号与 send 协程的消息 Channel 做映射，确保消息正确且有序。

### 3.3 重要成员方法

#### 3.3.1 `connect()` 方法

+ 连接**服务注册中心**和**RPC服务提供方**，并开启 **`send`** 和 **`recv`** 两个协程处理消息的收发。
+ 健康检测，自动发送心跳包。

```C++
bool RpcClient::connect(Address::ptr address)
{
    // 创建socket
    Socket::ptr sock = Socket::CreateTCP(address);

    if (!sock)
    {
        return false;
    }

    // 发起连接
    if (!sock->connect(address, m_timeout))
    {
        m_session = nullptr;
        return false;
    }
    m_isHeartClose = false;
    m_isClose = false;
    m_session = std::make_shared<RpcSession>(sock);
    m_chan = Channel<Protocol::ptr>(s_channel_capacity);

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

    // 自动发送心跳包
    if (m_auto_heartbeat)
    {
        m_heartTimer = IOManager::GetThis()->addTimer(
            30'000, [this]
            {
                DBSPIDER_LOG_DEBUG(g_logger) << "heart beat";
                if (m_isHeartClose)
                {
                    DBSPIDER_LOG_DEBUG(g_logger) << "Server closed";
                    close();
                }
                // 创建心跳包
                Protocol::ptr proto = Protocol::Create(Protocol::MsgType::HEARTBEAT_PACKET, "");
                // 向 send 协程的 Channel 发送消息
                m_chan << proto;
                m_isHeartClose = true; },
            true);
    }

    return true;
}
```

##### 3.3.1.1 `send()` 方法

+ **send** 协程调用，用于和RPC服务提供方和服务注册中心收发消息，通过消息发送通道 `m_chan`，只有 `m_chan` 有消息，就会向对方发送。
+ **send** 协程主要向对端发送以下消息，通过消息发送通道收集
  + 心跳包 `HEARTBEAT_PACKET`；
  + 服务调用请求 `RPC_METHOD_REQUEST`， `call()` 调用时，将消息写到消息发送通道；
  + 服务订阅请求 `RPC_SUBSCRIBE_REQUEST`，`subscribes()` 订阅时，将消息写到消息发送通道；
  + 服务发布响应 `RPC_PUBLISH_RESPONSE`，接收到服务发布消息后，向对端发送响应。

    ```C++
    void RpcClient::handleSend()
    {
        Protocol::ptr request;
        // 通过 Channel 收集调用请求，如果没有消息时 Channel 内部会挂起该协程等待消息到达
        // Channel 被关闭时会退出循环
        while (m_chan >> request)
        {
            if (!request)
            {
                DBSPIDER_LOG_WARN(g_logger) << "RpcClient::handleSend() fail";
                continue;
            }
            // 发送请求
            m_session->sendProtocol(request);
        }
    }
    ```

##### 3.3.1.2 `recv()` 方法

+ **recv** 协程调用，用于接收服务调用结果和服务发布消息。

    ```C++
    void RpcClient::handleRecv()
    {
        if (!m_session->isConnected())
        {
            return;
        }
        while (true)
        {
            // 接收响应
            Protocol::ptr response = m_session->recvProtocol();
            if (!response)
            {
                DBSPIDER_LOG_WARN(g_logger) << "RpcClient::handleRecv() fail";
                close();
                break;
            }
            m_isHeartClose = false;

            // 获取响应类型
            Protocol::MsgType type = response->getMsgType();
            // 判断响应类型进行对应的处理
            switch (type)
            {
            case Protocol::MsgType::HEARTBEAT_PACKET:
                m_isHeartClose = false;
                break;
            case Protocol::MsgType::RPC_METHOD_RESPONSE:
                // 处理调用结果
                handleMethodResponse(response);
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

+ `handleMethodResponse()` 方法

    ```C++
    void RpcClient::handleMethodResponse(Protocol::ptr response)
    {
        // 获取该调用结果的序列号
        uint32_t id = response->getSequenceId();
        std::map<uint32_t, Channel<Protocol::ptr>>::iterator it;

        MutexType::Lock lock(m_mutex);
        // 查找该序列号的 Channel 是否还存在，如果不存在直接返回
        it = m_responseHandle.find(id);
        if (it == m_responseHandle.end())
        {
            return;
        }
        // 通过序列号获取等待该结果的 Channel
        Channel<Protocol::ptr> chan = it->second;
        // 对该 Channel 发送调用结果唤醒调用者
        chan << response;
    }
    ```

+ `handlePublish()` 方法 ：从 `m_subHandle` 找到订阅服务对应的回调函数执行

    ```C++
    void RpcClient::handlePublish(Protocol::ptr proto)
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

#### 3.3.2 `call()` 方法

+ 四种调用方式

    ```C++
    // 有参数的调用
    template <typename R, typename... Params>
    Result<R> call(const std::string &name, Params... ps)
    {
        using args_type = std::tuple<typename std::decay<Params>::type...>;
        args_type args = std::make_tuple(ps...);
        Serializer s;
        s << name << args;
        s.reset();
        return call<R>(s);
    }

    // 无参数的调用
    template <typename R>
    Result<R> call(const std::string &name)
    {
        Serializer s;
        s << name;
        s.reset();
        return call<R>(s);
    }

    // 异步回调模式
    template <typename... Params>
    void callback(const std::string &name, Params &&...ps)
    {
        static_assert(sizeof...(ps), "without a callback function");
        auto tp = std::make_tuple(ps...);
        constexpr auto size = std::tuple_size<typename std::decay<decltype(tp)>::type>::value;
        auto cb = std::get<size - 1>(tp);
        static_assert(function_traits<decltype(cb)>{}.arity == 1, "callback type not support");
        using res = typename function_traits<decltype(cb)>::template args<0>::type;
        using rt = typename res::row_type;
        static_assert(std::is_invocable_v<decltype(cb), Result<rt>>, "callback type not support");
        RpcClient::ptr self = shared_from_this();
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

    // 异步调用，返回一个 Channel
    template <typename R, typename... Params>
    Channel<Result<R>> async_call(const std::string &name, Params &&...ps)
    {
        Channel<Result<R>> chan(1);
        RpcClient::ptr self = shared_from_this();
        go[self, chan, name, ps..., this]() mutable
        {
            chan << call<R>(name, ps...);
            self = nullptr;
        };
        return chan;
    }
    ```

+ 实际调用方法

    ```C++
    template <typename R>
    Result<R> call(Serializer s)
    {
        Result<R> val;
        if (!m_session || !m_session->isConnected())
        {
            val.setCode(RPC_CLOSED);
            val.setMsg("socket closed");
            return val;
        }

        // 开启一个 Channel 接收调用结果
        Channel<Protocol::ptr> recvChan(1);

        // 本次调用的序列号
        uint32_t id = 0;
        std::map<uint32_t, Channel<Protocol::ptr>>::iterator it;
        {
            MutexType::Lock lock(m_mutex);
            id = m_sequenceId;
            // 将请求序列号与接收 Channel 关联
            it = m_responseHandle.emplace(m_sequenceId, recvChan).first;
            ++m_sequenceId;
        }

        // 创建请求协议，附带上请求 id
        Protocol::ptr request =
            Protocol::Create(Protocol::MsgType::RPC_METHOD_REQUEST, s.toString(), id);

        // 向 send 协程的 Channel 发送消息
        m_chan << request;

        bool timeout = false;
        Protocol::ptr response;
        if (!recvChan.waitFor(response, m_timeout))
        {
            timeout = true;
        }

        {
            MutexType::Lock lock(m_mutex);
            if (!m_isClose)
            {
                // 删除序列号与 Channel 的映射
                m_responseHandle.erase(it);
            }
        }

        if (timeout)
        {
            // 超时
            val.setCode(RPC_TIMEOUT);
            val.setMsg("call timeout");
            return val;
        }

        if (!response)
        {
            val.setCode(RPC_CLOSED);
            val.setMsg("socket closed");
            return val;
        }

        if (response->getContent().empty())
        {
            val.setCode(RPC_NO_METHOD);
            val.setMsg("Method not find");
            return val;
        }

        Serializer serializer(response->getContent());
        try
        {
            serializer >> val;
        }
        catch (...)
        {
            val.setCode(RPC_NO_MATCH);
            val.setMsg("return value not match");
        }
        return val;
    }
    ```

#### 3.3.3 `subscirbe()` 方法

+ 向服务注册中心或RPC服务端订阅消息

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
                return;
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
