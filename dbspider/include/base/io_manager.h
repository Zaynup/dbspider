#pragma once

#include <atomic>
#include <memory>

#include "scheduler.h"
#include "sync.h"
#include "timer.h"

namespace dbspider
{
    // 基于Epoll的IO协程调度器
    class IOManager : public Scheduler, public TimeManager
    {
    public:
        using ptr = std::shared_ptr<IOManager>;
        using RWMutexType = RWMutex;

        // IO事件
        enum Event
        {
            NONE = 0x0, // 无事件
            READ = 0x1, // 读事件(EPOLLIN)
            WRITE = 0x4 // 写事件(EPOLLOUT)
        };

    private:
        // Socket事件上下文类
        struct FdContext
        {
            using MutexType = Mutex;

            // 事件上下文类
            struct EventContext
            {
                Scheduler *scheduler = nullptr; // 事件执行的调度器
                Fiber::ptr fiber;               // 事件的协程
                std::function<void()> cb;       // 事件的回调函数

                bool empty()
                {
                    return !scheduler && !fiber && !cb;
                }
            };

            // 获取事件上下文类
            EventContext &getContext(Event event);

            // 重置事件上下文
            void resetContext(EventContext &event);

            // 触发事件
            void triggerEvent(Event event);

            int fd = 0;          // 事件关联句柄
            EventContext read;   // 读事件上下文
            EventContext write;  // 写事件上下文
            Event events = NONE; // 注册的事件类型
            MutexType mutex;     // 事件的Mutex
        };

    public:
        IOManager(size_t threads = 1, const std::string &name = "");

        ~IOManager();

        // 添加事件
        bool addEvent(int fd, Event event, std::function<void()> cb = nullptr);

        // 删除事件
        bool delEvent(int fd, Event event);

        // 取消事件
        bool cancelEvent(int fd, Event event);

        // 取消所有事件
        bool cancelAllEvent(int fd);

        // 返回当前的IOManager
        static IOManager *GetThis();

    protected:
        void notify() override;
        void wait() override;
        bool stopping() override;
        // 重置socket句柄上下文的容器大小
        void contextResize(size_t size);
        void onInsertAtFront() override;

        // 判断是否可以停止
        bool stopping(uint64_t &timeout);

    private:
        int m_epfd;                                    // epoll 文件句柄
        int m_tickleFds[2];                            // pipe 文件句柄
        std::atomic<size_t> m_pendingEventCount = {0}; // 当前等待执行的事件数量
        RWMutexType m_mutex;                           // IOManager的mutex
        std::vector<FdContext *> m_fdContexts;         // socket事件上下文的容器
    };

#define go (*dbspider::IOManager::GetThis()) +
#define Go (*dbspider::IOManager::GetThis()) + [=]() mutable
}