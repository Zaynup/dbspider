#pragma once

#include <functional>
#include <memory>
#include "socket_stream.h"
#include "tcp_server.h"
#include "log.h"
#include "sync.h"
#include "traits.h"
#include "protocol.h"
#include "rpc.h"
#include "rpc_session.h"

namespace dbspider::rpc
{
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
        void registerMethod(const std::string &name, Func func)
        {
            m_handlers[name] = [func, this](Serializer serializer, const std::string &arg)
            {
                proxy(func, serializer, arg);
            };
        }

        // 设置RPC服务器名称
        void setName(const std::string &name) override;

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

    protected:
        // 向服务注册中心发起注册
        void registerService(const std::string &name);

        // 调用服务端注册的函数，返回序列化完的结果
        Serializer call(const std::string &name, const std::string &arg);

        // 调用代理
        template <typename F>
        void proxy(F fun, Serializer serializer, const std::string &arg)
        {
            typename function_traits<F>::stl_function_type func(fun);
            using Return = typename function_traits<F>::return_type;
            using Args = typename function_traits<F>::tuple_type;

            dbspider::rpc::Serializer s(arg);
            // 反序列化字节流，存为参数tuple
            Args args;
            try
            {
                s >> args;
            }
            catch (...)
            {
                Result<Return> val;
                val.setCode(dbspider::rpc::RPC_NO_MATCH);
                val.setMsg("params not match");
                serializer << val;
                return;
            }

            return_type_t<Return> rt{};

            constexpr auto size = std::tuple_size<typename std::decay<Args>::type>::value;
            auto invoke = [&func, &args ]<std::size_t... Index>(std::index_sequence<Index...>)
            {
                return func(std::get<Index>(std::forward<Args>(args))...);
            };

            if constexpr (std::is_same_v<Return, void>)
            {
                invoke(std::make_index_sequence<size>{});
            }
            else
            {
                rt = invoke(std::make_index_sequence<size>{});
            }

            Result<Return> val;
            val.setCode(dbspider::rpc::RPC_SUCCESS);
            val.setVal(rt);
            serializer << val;
        }

        // 更新心跳定时器
        void update(Timer::ptr &heartTimer, Socket::ptr client);

        // 处理客户端连接
        void handleClient(Socket::ptr client) override;

        // 处理客户端过程调用请求
        Protocol::ptr handleMethodCall(Protocol::ptr proto);

        // 处理心跳包
        Protocol::ptr handleHeartbeatPacket(Protocol::ptr proto);

        // 处理订阅请求
        Protocol::ptr handleSubscribe(Protocol::ptr proto, RpcSession::ptr client);

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
}
