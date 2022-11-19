#include "scheduler.h"

#include <algorithm>
#include <signal.h>
#include "hook.h"
#include "scheduler.h"
#include "macro.h"
#include "log.h"
#include "util.h"

namespace dbspider
{
    static Logger::ptr g_logger = DBSPIDER_LOG_NAME("system");

    // 当前线程的调度器，同一个调度器下的所有线程指向同一个调度器实例
    static thread_local Scheduler *t_scheduler = nullptr;

    Scheduler::Scheduler(size_t threads, const std::string &name)
        : m_name(name), m_threadCount(threads)
    {
        t_scheduler = this;
    }

    Scheduler::~Scheduler()
    {
        DBSPIDER_ASSERT(m_stop);
        if (GetThis() == this)
        {
            t_scheduler = nullptr;
        }
    }

    // 启动调度器
    void Scheduler::start()
    {
        MutexType::Lock lock(m_mutex);
        // 调度器没有停止就直接返回
        if (m_stop == false)
        {
            return;
        }
        m_stop = false;
        DBSPIDER_ASSERT(m_threads.empty());
        m_threads.resize(m_threadCount);
        m_threadIds.resize(m_threadCount);
        for (size_t i = 0; i < m_threadCount; ++i)
        {
            m_threads[i].reset(new dbspider::Thread(m_name + "_" + std::to_string(i),
                                                    [this]
                                                    {
                                                        this->run();
                                                    }));
            m_threadIds[i] = m_threads[i]->getId();
        }
    }

    // 停止协程调度器
    void Scheduler::stop()
    {
        m_stop = true;
        for (size_t i = 0; i < m_threadCount; ++i)
        {
            notify();
        }
        std::vector<Thread::ptr> vec;
        vec.swap(m_threads);
        for (auto &t : vec)
        {
            t->join();
        }
    }

    Scheduler *Scheduler::GetThis()
    {
        return t_scheduler;
    }

    // 通知协程调度器有任务了
    void Scheduler::notify()
    {
        DBSPIDER_LOG_INFO(g_logger) << "notify";
    }

    // 协程调度函数
    void Scheduler::run()
    {
        signal(SIGPIPE, SIG_IGN);
        setThis();
        dbspider::Fiber::EnableFiber();

        dbspider::set_hook_enable(true);
        Fiber::ptr cb_fiber;
        Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::wait, this)));
        ScheduleTask task;

        while (true)
        {
            task.reset();
            bool tickle = false; // 是否tickle其他线程进行任务调度
            // 线程取出任务
            {
                MutexType::Lock lock(m_mutex);
                auto it = m_tasks.begin();
                // 遍历所有调度任务
                while (it != m_tasks.end())
                {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                    if (it->thread != -1 && GetThreadId() != it->thread)
                    {
                        ++it;
                        tickle = true;
                        continue;
                    }
                    // 找到一个未指定线程，或是指定了当前线程的任务
                    DBSPIDER_ASSERT(*it);

                    // [BUG FIX]: hook IO相关的系统调用时，在检测到IO未就绪的情况下，会先添加对应的读写事件，再yield当前协程，等IO就绪后再resume当前协程
                    // 多线程高并发情境下，有可能发生刚添加事件就被触发的情况，如果此时当前协程还未来得及yield，则这里就有可能出现协程状态仍为EXEC的情况
                    // 这里简单地跳过这种情况，以损失一点性能为代价。
                    if (it->fiber && it->fiber->getState() == Fiber::EXEC)
                    {
                        ++it;
                        continue;
                    }

                    // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                    task = *it;
                    m_tasks.erase(it++);
                    break;
                }
                // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
                if (it != m_tasks.end())
                {
                    tickle = true;
                }
            }
            if (tickle)
            {
                notify();
            }

            if (task.fiber && (task.fiber->getState() != Fiber::TERM && task.fiber->getState() != Fiber::EXCEPT))
            {
                ++m_activeThreads;
                task.fiber->resume();
                --m_activeThreads;
                if (task.fiber->getState() == Fiber::READY)
                {
                    submit(task.fiber);
                }
                task.reset();
            }
            else if (task.cb)
            {
                if (cb_fiber)
                {
                    cb_fiber->reset(task.cb);
                }
                else
                {
                    cb_fiber.reset(new Fiber(task.cb));
                }
                task.reset();
                ++m_activeThreads;
                cb_fiber->resume();
                --m_activeThreads;
                if (cb_fiber->getState() == Fiber::READY)
                {
                    submit(cb_fiber);
                    cb_fiber.reset();
                }
                else if (cb_fiber->isTerminate())
                {
                    cb_fiber->reset(nullptr);
                }
                else
                {
                    cb_fiber = nullptr;
                }
            }
            else
            {
                // 进到这个分支情况一定是任务队列空了，调度idle协程即可
                if (idle_fiber->getState() == Fiber::TERM)
                {
                    // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，
                    // 不会结束，如果idle协程结束了，那一定是调度器停止了
                    break;
                }
                ++m_idleThreads;
                idle_fiber->resume();
                --m_idleThreads;
            }
        }
    }

    // 返回是否可以停止
    bool Scheduler::stopping()
    {
        MutexType::Lock lock(m_mutex);
        return m_stop && m_tasks.empty() && m_activeThreads == 0;
    }

    // 协程无任务可调度时执行wait协程
    void Scheduler::wait()
    {
        DBSPIDER_LOG_INFO(g_logger) << "idle";
        while (!stopping())
        {
            dbspider::Fiber::YieldToHold();
        }
        DBSPIDER_LOG_DEBUG(g_logger) << "idle fiber exit";
    }

    // 设置当前的协程调度器
    void Scheduler::setThis()
    {
        t_scheduler = this;
    }
}