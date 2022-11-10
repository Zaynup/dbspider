#pragma once

#include <memory>
#include <string>
#include <vector>
#include <list>
#include <map>
#include "sync.h"
#include "tcp_server.h"
#include "socket_stream.h"
#include "protocol.h"
#include "rpc_session.h"
#include "serializer.h"

namespace dbspider::rpc
{
    /**
     * RPC服务注册中心
     * 1.接收客户端服务发现请求。
     * 2.接收服务端服务注册请求，断开连接后移除服务。
     */
    class RpcServiceRegistry : public TcpServer
    {
    public:
        using ptr = std::shared_ptr<RpcServiceRegistry>;
        using MutexType = CoMutex;
        RpcServiceRegistry(IOManager *worker = IOManager::GetThis(),
                           IOManager *accept_worker = IOManager::GetThis());

        ~RpcServiceRegistry();

        // 设置 RPC 服务注册中心名称
        void setName(const std::string &name) override { TcpServer::setName(name); }

        /**
         * 发布消息
         * key: 发布的 key
         * data: 支持 Serializer 的都可以发布
         */
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
        // 更新心跳定时器
        void update(Timer::ptr &heartTimer, Socket::ptr client);

        // 处理端请求
        void handleClient(Socket::ptr client) override;

        // 为服务端提供服务注册:将服务地址注册到对应服务名下,断开连接后地址自动清除
        Protocol::ptr handleRegisterService(Protocol::ptr p, Address::ptr address);

        // 移除注册服务
        void handleUnregisterService(Address::ptr address);

        // 为客户端提供服务发现
        // 返回值：服务地址列表
        Protocol::ptr handleDiscoverService(Protocol::ptr p);

        // 处理 provider 初次连接时的事件，获取开放服务的端口
        Address::ptr handleProvider(Protocol::ptr p, Socket::ptr sock);

        // 处理心跳包
        Protocol::ptr handleHeartbeatPacket(Protocol::ptr p);

        // 处理订阅请求
        Protocol::ptr handleSubscribe(Protocol::ptr proto, RpcSession::ptr client);

    private:
        std::multimap<std::string, std::string> m_services;                                            // 维护服务名和服务地址列表的多重映射
        std::map<std::string, std::vector<std::multimap<std::string, std::string>::iterator>> m_iters; // 维护服务地址到迭代器的映射
        MutexType m_mutex;
        uint64_t m_AliveTime;                                                         // 允许心跳超时的时间 默认 40s
        std::unordered_multimap<std::string, std::weak_ptr<RpcSession>> m_subscribes; // 订阅的客户端
        MutexType m_sub_mtx;                                                          // 保护 m_subscribes
        bool m_stop_clean = false;                                                    // 停止清理订阅协程
        Channel<bool> m_clean_chan{1};                                                // 等待清理协程停止
    };

}
