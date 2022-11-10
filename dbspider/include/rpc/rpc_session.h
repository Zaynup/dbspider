#pragma once

#include "socket_stream.h"
#include "protocol.h"

namespace dbspider::rpc
{
    // rpc session 封装了协议的收发
    class RpcSession : public SocketStream
    {
    public:
        using ptr = std::shared_ptr<RpcSession>;
        using MutexType = CoMutex;

        // 构造函数 owner 是否托管Socket
        RpcSession(Socket::ptr socket, bool owner = true);

        // 接收协议
        Protocol::ptr recvProtocol();

        // 发送协议
        ssize_t sendProtocol(Protocol::ptr proto);

    private:
        MutexType m_mutex;
    };
}
