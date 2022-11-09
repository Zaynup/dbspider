#pragma once

#include <memory>
#include "io_manager.h"
#include "sync.h"

namespace dbspider
{
    // 文件句柄上下文类
    class FdCtx : std::enable_shared_from_this<FdCtx>
    {
    public:
        using ptr = std::shared_ptr<FdCtx>;

        // 通过文件句柄构造FdCtx
        FdCtx(int fd);

        ~FdCtx();

        // 初始化
        bool init();

        // 是否初始化完成
        bool isInit() const { return m_isInit; }

        // 是否socket
        bool isSocket() const { return m_isSocket; }

        // 是否已关闭
        bool isClose() const { return m_isClosed; }

        // 设置系统非阻塞
        void setSysNonblock(bool f) { m_sysNonblock = f; }

        // 获取系统非阻塞
        bool getSysNonblock() const { return m_sysNonblock; }

        // 设置用户主动设置非阻塞
        void setUserNonblock(bool f) { m_userNonblock = f; }

        // 获取是否用户主动设置的非阻塞
        bool getUserNonblock() const { return m_userNonblock; }

        void setSendTimeout(uint64_t timeout) { m_sendTimeout = timeout; }
        uint64_t getSendTimeout() const { return m_sendTimeout; }

        void setRecvTimeout(uint64_t timeout) { m_sendTimeout = timeout; }
        uint64_t getRecvTimeout() const { return m_recvTimeout; }

        //  设置超时时间
        void setTimeout(int type, uint64_t timeout);

        // 获取超时时间
        uint64_t getTimeout(int type) const;

    private:
        bool m_isInit : 1;       // 是否初始化
        bool m_isSocket : 1;     // 是否是socket
        bool m_sysNonblock : 1;  // 是否hook非阻塞
        bool m_userNonblock : 1; // 用户主动设置非阻塞
        bool m_isClosed : 1;     // 是否关闭
        int m_fd = 0;            // 文件句柄
        uint64_t m_sendTimeout;  // 写超时间毫秒
        uint64_t m_recvTimeout;  // 读超时间毫秒
        dbspider::IOManager *m_iomanager;
    };

    // 文件句柄管理类
    class FdManager
    {
    public:
        using RWMutexType = dbspider::RWMutex;

        FdManager();

        // 获取/创建文件句柄类FdCtx
        FdCtx::ptr get(int fd, bool auto_create = false);

        // 删除文件句柄类
        void del(int fd);

    private:
        RWMutexType m_mutex;
        std::vector<FdCtx::ptr> m_fds;
    };

    // 文件句柄单例
    using FdMgr = Singleton<FdManager>;
}