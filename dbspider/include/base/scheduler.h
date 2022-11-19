#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <queue>
#include <vector>
#include "fiber.h"
#include "sync.h"
#include "macro.h"
#include "thread.h"

namespace dbspider
{
    class Scheduler
    {
    public:
        using ptr = std::shared_ptr<Scheduler>;
        using MutexType = Mutex;

        Scheduler(size_t threads = 4, const std::string &name = "");

        virtual ~Scheduler();

        const std::string &getName() const { return m_name; }

        // 启动调度器
        void start();

        // 停止协程调度器
        void stop();

        // 提交任务到调度器，任务可以是协程或者可调用对象
        template <typename FiberOrCb>
        [[maybe_unused]] Scheduler *submit(FiberOrCb &&fc, int thread = -1)
        {
            bool need_notify = false;
            {
                MutexType::Lock lock(m_mutex);
                need_notify = submitNoLock(std::forward<FiberOrCb>(fc), thread);
            }
            if (need_notify)
            {
                notify();
            }
            return this;
        }

        // 批量调度协程
        template <class InputIterator>
        void submit(InputIterator begin, InputIterator end)
        {
            bool need_notify = false;
            {
                MutexType::Lock lock(m_mutex);
                while (begin != end)
                {
                    need_notify = submitNoLock(std::move(*begin), -1) || need_notify;
                    ++begin;
                }
            }
            if (need_notify)
            {
                notify();
            }
        }

        template <class FiberOrCb>
        [[maybe_unused]] Scheduler *operator+(FiberOrCb &&fc)
        {
            return submit(std::move(fc));
        }

        static Scheduler *GetThis();

    protected:
        // 通知协程调度器有任务了
        virtual void notify();

        // 协程调度函数
        void run();

        // 返回是否可以停止
        virtual bool stopping();

        // 协程无任务可调度时执行wait协程
        virtual void wait();

        // 设置当前的协程调度器
        void setThis();

        // 是否有空闲线程
        bool hasIdleThreads() { return m_idleThreads > 0; }

    private:
        // 添加调度任务（无锁）
        template <typename FiberOrCb>
        bool submitNoLock(FiberOrCb &&fc, int thread)
        {
            bool need_notify = m_tasks.empty();
            ScheduleTask task(std::forward<FiberOrCb>(fc), thread);
            if (task)
            {
                m_tasks.push_back(task);
            }
            return need_notify;
        }

    private:
        struct ScheduleTask
        {
            Fiber::ptr fiber;
            std::function<void()> cb;
            int thread;

            ScheduleTask(Fiber::ptr &f, int t = -1)
            {
                fiber = f;
                thread = t;
            }
            ScheduleTask(Fiber::ptr &&f, int t = -1)
            {
                fiber = std::move(f);
                thread = t;
                f = nullptr;
            }
            ScheduleTask(std::function<void()> &f, int t = -1)
            {
                cb = f;
                thread = t;
            }
            ScheduleTask(std::function<void()> &&f, int t = -1)
            {
                cb = std::move(f);
                thread = t;
                f = nullptr;
            }
            ScheduleTask()
            {
                thread = -1;
            }
            void reset()
            {
                thread = -1;
                fiber = nullptr;
                cb = nullptr;
            }
            operator bool()
            {
                return fiber || cb;
            }
        };

    private:
        MutexType m_mutex;                  // 互斥锁
        std::vector<Thread::ptr> m_threads; // 调度器线程池
        std::list<ScheduleTask> m_tasks;    // 调度器任务
        std::string m_name;                 // 调度器名字

    protected:
        std::vector<int> m_threadIds;            // 线程池的线程ID数组
        size_t m_threadCount = 0;                // 线程数量
        std::atomic<size_t> m_activeThreads = 0; // 活跃线程数
        std::atomic<size_t> m_idleThreads = 0;   // 空闲线程数
        bool m_stop = true;                      // 调度其是否停止
    };
}