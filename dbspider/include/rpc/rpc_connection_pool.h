#pragma once

#include "traits.h"
#include "sync.h"
#include "route_strategy.h"
#include "rpc_client.h"
#include "serializer.h"
#include "rpc_session.h"

namespace dbspider::rpc
{
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

        // 异步远程过程调用回调模式
        template <typename... Params>
        void callback(const std::string &name, Params &&...ps)
        {
            static_assert(sizeof...(ps), "without a callback function");

            // 1. 将参数打包为 tuple
            auto tp = std::make_tuple(ps...);

            // 2. 获取参数大小
            constexpr auto size = std::tuple_size<typename std::decay<decltype(tp)>::type>::value;

            // 3. 获取异步回调函数（最后一个参数为回调函数）
            auto cb = std::get<size - 1>(tp);

            static_assert(function_traits<decltype(cb)>{}.arity == 1, "callback type not support");

            // 4. 获取返回结果类型 Result<type>
            using res = typename function_traits<decltype(cb)>::template args<0>::type;

            // 5. 获取返回结果的原始类型 type
            using rt = typename res::row_type;

            static_assert(std::is_invocable_v<decltype(cb), Result<rt>>, "callback type not support");

            RpcConnectionPool::ptr self = shared_from_this();

            // 6. 开启协程执行回调函数
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

        // 异步远程过程调用
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

        // 远程过程调用
        template <typename R, typename... Params>
        Result<R> call(const std::string &name, Params... ps)
        {
            MutexType::Lock lock(m_connMutex);
            // 从连接池里取出服务连接
            auto conn = m_conns.find(name);
            Result<R> result;

            // 在连接池中找到服务名对应连接
            if (conn != m_conns.end())
            {
                lock.unlock();
                result = conn->second->template call<R>(name, ps...);
                if (result.getCode() != RPC_CLOSED)
                {
                    // 如果连接未关闭且正常获取结果，直接返回调用结果。
                    return result;
                }
                lock.lock();
                // 连接关闭，移除失效连接
                std::vector<std::string> &addrs = m_serviceCache[name];
                std::erase(addrs, conn->second->getSocket()->getRemoteAddress()->toString());
                m_conns.erase(name);
            }

            // 若未在连接池中找到对应连接，去缓存中找服务地址
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

            // 选择客户端负载均衡策略
            RouteStrategy<std::string>::ptr strategy =
                RouteEngine<std::string>::queryStrategy(Strategy::Random);

            if (addrs.size())
            {
                // 根据路由策略选择服务地址
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

        void close();

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
}
