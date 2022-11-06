#include "io_manager.h"

#include <error.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "config.h"
#include "io_manager.h"
#include "log.h"
#include "macro.h"

namespace dbspider
{
    static Logger::ptr g_logger = DBSPIDER_LOG_NAME("system");

    static ConfigVar<uint64_t>::ptr g_scheduler_threads =
        Config::Lookup<uint64_t>("scheduler.threads", 4,
                                 "scheduler default threads");
    static ConfigVar<std::string>::ptr g_scheduler_name =
        Config::Lookup<std::string>("scheduler.name", "main",
                                    "scheduler default name");

    static uint64_t s_scheduler_threads = 0;
    static std::string s_scheduler_name;

    struct _IOManagerIniter
    {
        _IOManagerIniter()
        {
            s_scheduler_threads = g_scheduler_threads->getValue();
            s_scheduler_name = g_scheduler_name->getValue();

            g_scheduler_threads->addListener(
                [](const uint64_t &old_val, const uint64_t &new_val)
                {
                    DBSPIDER_LOG_INFO(g_logger) << "scheduler threads from "
                                                << old_val << " to " << new_val;
                    s_scheduler_threads = new_val;
                });

            g_scheduler_name->addListener(
                [](const std::string &old_val, const std::string &new_val)
                {
                    DBSPIDER_LOG_INFO(g_logger) << "scheduler name from "
                                                << old_val << " to " << new_val;
                    s_scheduler_name = new_val;
                });
        }
    };

    static _IOManagerIniter s_initer;

    // 获取事件上下文类
    IOManager::FdContext::EventContext &IOManager::FdContext::getContext(IOManager::Event event)
    {
        switch (event)
        {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            DBSPIDER_ASSERT2(false, "getContext");
        }
        throw std::invalid_argument("getContext invalid event");
    }

    // 重置事件上下文
    void IOManager::FdContext::resetContext(IOManager::FdContext::EventContext &event)
    {
        event.scheduler = nullptr;
        event.fiber.reset();
        event.cb = nullptr;
    }

    // 触发事件
    void IOManager::FdContext::triggerEvent(IOManager::Event event)
    {
        DBSPIDER_ASSERT2(events & event, std::to_string(event) + " & " + std::to_string(events) + " = " + std::to_string(events & event));
        if (!(events & event))
        {
            DBSPIDER_LOG_ERROR(DBSPIDER_LOG_ROOT()) << "ASSERTION: " << (events & event)
                                                    << "\n"
                                                    << std::to_string(event) + " & " + std::to_string(events) + " = " + std::to_string(events & event) << "\n"
                                                    << "\nbacktrace:\n"
                                                    << dbspider::BacktraceToString(100, 2, "    ");
            assert(events & event);
        }
        events = (Event)(events & ~event);
        EventContext &eventContext = getContext(event);
        if (eventContext.cb)
        {
            eventContext.scheduler->submit(std::move(eventContext.cb));
        }
        else
        {
            eventContext.scheduler->submit(std::move(eventContext.fiber));
        }
        eventContext.scheduler = nullptr;
    }

    IOManager::IOManager(size_t threads, const std::string &name)
        : Scheduler(threads, name)
    {
        int rt = pipe(m_tickleFds);
        DBSPIDER_ASSERT(!rt);

        m_epfd = epoll_create(1);
        DBSPIDER_ASSERT(m_epfd > 0);

        epoll_event event;
        memset(&event, 0, sizeof(epoll_event));
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = m_tickleFds[0];

        rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
        DBSPIDER_ASSERT(!rt);

        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
        DBSPIDER_ASSERT(!rt);

        contextResize(64);

        start();
    }

    IOManager::~IOManager()
    {
        sleep(3);
        m_stop = true;
        while (!stopping())
        {
            sleep(3);
        }
        stop();
        close(m_epfd);
        close(m_tickleFds[0]);
        close(m_tickleFds[1]);

        for (size_t i = 0; i < m_fdContexts.size(); ++i)
        {
            if (m_fdContexts[i])
            {
                delete m_fdContexts[i];
            }
        }
    }

    // 添加事件
    bool IOManager::addEvent(int fd, Event event, std::function<void()> cb)
    {
        DBSPIDER_LOG_DEBUG(g_logger) << "addEvent() : fd=" << fd << " event=" << (event == 1 ? "read" : "write");
        FdContext *fdContext = nullptr;
        RWMutexType::ReadLock lock(m_mutex);
        if ((int)m_fdContexts.size() > fd)
        {
            fdContext = m_fdContexts[fd];
            lock.unlock();
        }
        else
        {
            lock.unlock();
            RWMutexType::WriteLock lock1(m_mutex);
            contextResize(fd * 1.5);
            fdContext = m_fdContexts[fd];
        }
        FdContext::MutexType::Lock lock2(fdContext->mutex);
        if (fdContext->events & event)
        {
            DBSPIDER_LOG_ERROR(g_logger) << "fd=" << fd << " addEvent fail, event already register. "
                                         << "event=" << event << " FdContext->event=" << fdContext->events;
            DBSPIDER_ASSERT(!(fdContext->events & event));
        }
        int op = fdContext->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        Event newEvent = (Event)(event | fdContext->events);
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epoll_event));
        epevent.events = EPOLLET | newEvent;
        epevent.data.ptr = fdContext;
        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fd << ", "
                                         << epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        m_pendingEventCount++;
        fdContext->events = newEvent;
        FdContext::EventContext &eventContext = fdContext->getContext(event);
        DBSPIDER_ASSERT(eventContext.empty());
        eventContext.scheduler = Scheduler::GetThis();
        if (cb)
        {
            eventContext.cb.swap(cb);
        }
        else
        {
            eventContext.fiber = Fiber::GetThis();
            DBSPIDER_ASSERT(eventContext.fiber->getState() == Fiber::EXEC);
        }

        return true;
    }

    // 删除事件
    bool IOManager::delEvent(int fd, Event event)
    {
        return true;
    }

    // 取消事件
    bool IOManager::cancelEvent(int fd, Event event)
    {
        return true;
    }

    // 取消所有事件
    bool IOManager::cancelAllEvent(int fd)
    {
        return true;
    }

    // 返回当前的IOManager
    IOManager *IOManager::GetThis()
    {
        // 默认调度器
        static IOManager s_scheduler(s_scheduler_threads, s_scheduler_name);
        IOManager *iom = dynamic_cast<IOManager *>(Scheduler::GetThis());
        return iom ? iom : &s_scheduler;
    }

    void IOManager::notify()
    {
        // 没有空闲线程返回
        if (!hasIdleThreads())
        {
            return;
        }
        int rt = write(m_tickleFds[1], "N", 1);
        DBSPIDER_ASSERT(rt == 1);
    }

    void IOManager::wait()
    {
    }

    bool IOManager::stopping()
    {
        uint64_t timeout = 0;
        return stopping(timeout);
    }

    // 重置socket句柄上下文的容器大小
    void IOManager::contextResize(size_t size)
    {
        size_t old_size = m_fdContexts.size();
        m_fdContexts.resize(size);
        for (size_t i = old_size; i < m_fdContexts.size(); ++i)
        {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }

    void IOManager::onInsertAtFront()
    {
        notify();
    }

    // 判断是否可以停止
    bool IOManager::stopping(uint64_t &timeout)
    {
        timeout = getNextTimer();
        return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
    }
}
