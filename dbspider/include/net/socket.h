#pragma once

#include <memory>
#include "address.h"
#include "bytearray.h"
#include "noncopyable.h"

namespace dbspider
{
    class Socket : public std::enable_shared_from_this<Socket>, Noncopyable
    {
    public:
        using ptr = std::shared_ptr<Socket>;
        using weak_ptr = std::weak_ptr<Socket>;

        enum Type
        {
            TCP = SOCK_STREAM,
            UDP = SOCK_DGRAM
        };

        enum Family
        {
            IPv4 = AF_INET,
            IPv6 = AF_INET6,
            UNIX = AF_UNIX
        };

        Socket(int family, int type, int protocol);
        virtual ~Socket();

        static Socket::ptr CreateTCP(Address::ptr address);
        static Socket::ptr CreateUDP(Address::ptr address);

        static Socket::ptr CreateTCPSocket();
        static Socket::ptr CreateUDPSocket();

        static Socket::ptr CreateTCPSocket6();
        static Socket::ptr CreateUDPSocket6();

        static Socket::ptr CreateUnixTCPSocket();
        static Socket::ptr CreateUnixUDPSocket();

        void setSendTimeout(uint64_t t);
        uint64_t getSendTimeout();

        void setRecvTimeout(uint64_t t);
        uint64_t getRecvTimeout();

        bool getOption(int level, int option, void *result, size_t *len);

        template <class T>
        bool getOption(int level, int option, T *result)
        {
            size_t len = sizeof(T);
            return getOption(level, option, result, &len);
        }

        bool setOption(int level, int option, void *result, size_t len);
        template <class T>
        bool setOption(int level, int option, T *result)
        {
            return setOption(level, option, result, sizeof(T));
        }

        Socket::ptr accept();
        bool bind(const Address::ptr address);
        bool connect(const Address::ptr address, uint64_t timeout_ms = -1);
        bool listen(int backlog = SOMAXCONN);
        bool close();

        // 发送数据
        virtual ssize_t send(const void *buffer, size_t length, int flags = 0);

        // 发送数据
        virtual ssize_t send(const iovec *buffers, size_t length, int flags = 0);

        // 发送数据
        virtual ssize_t sendTo(const void *buffer, size_t length, const Address::ptr to, int flags = 0);

        // 发送数据
        virtual ssize_t sendTo(const iovec *buffers, size_t length, const Address::ptr to, int flags = 0);

        // 接受数据
        virtual ssize_t recv(void *buffer, size_t length, int flags = 0);

        // 接受数据
        virtual ssize_t recv(iovec *buffers, size_t length, int flags = 0);

        // 接受数据
        virtual ssize_t recvFrom(void *buffer, size_t length, Address::ptr from, int flags = 0);

        // 接受数据
        virtual ssize_t recvFrom(iovec *buffers, size_t length, Address::ptr from, int flags = 0);

        Address::ptr getRemoteAddress();
        Address::ptr getLocalAddress();

        int getSocket() const { return m_sock; };
        int getFamily() const { return m_family; };
        int getType() const { return m_type; };
        int getProtocol() const { return m_protocol; };
        bool isConnected() const { return m_isConnected; };
        bool isValid() const { return m_sock != -1; };
        int getError();

        std::ostream &dump(std::ostream &os) const;
        std::string toString() const;

        bool cancelRead();
        bool cancelWrite();
        bool cancelAccept();
        bool cancelAll();

    private:
        void initSocket();
        void newSocket();
        bool init(int sock);

    private:
        int m_sock;
        int m_family;
        int m_type;
        int m_protocol;

        bool m_isConnected = false;

        Address::ptr m_remoteAddress;
        Address::ptr m_localAddress;
    };
}
