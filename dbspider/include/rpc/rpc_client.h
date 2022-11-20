#pragma once

#include <memory>
#include <functional>
#include <future>
#include "io_manager.h"
#include "net/socket.h"
#include "net/socket_stream.h"
#include "sync.h"
#include "traits.h"
#include "protocol.h"
#include "route_strategy.h"
#include "rpc.h"
#include "rpc_session.h"

namespace dbspider::rpc
{
    /**
     * RPC客户端
     * 开启一个 send 协程，通过 Channel 接收调用请求，并转发 request 请求给服务器。
     * 开启一个 recv 协程，负责接收服务器发送的 response 响应并通过序列号获取对应调用者的 Channel，
     * 将 response 放入 Channel 唤醒调用者。
     * 所有调用者的 call 请求将通过 Channel 发送给 send 协程， 然后开启一个 Channel 用于接收 response 响应，
     * 并将请求序列号与自己的 Channel 关联起来。
     */
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

        // 设置RPC调用超时时间
        void setTimeout(uint64_t timeout_ms);

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

        Socket::ptr getSocket() { return m_session->getSocket(); }

        bool isClose() { return !m_session || !m_session->isConnected(); }

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
}
