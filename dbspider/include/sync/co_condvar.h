#pragma once

#include "noncopyable.h"
#include "mutex.h"

namespace dbspider
{
    class Fiber;
    class Timer;

    class CoCondvar : Noncopyable
    {
    public:
        using MutexType = SpinLock;

        void notify();

        void notifyAll();

        void wait();

        void wait(CoMutex::Lock &lock);

    private:
        std::queue<std::shared_ptr<Fiber>> m_waitQueue; // 协程等待队列
        MutexType m_mutex;                              // 保护协程等待队列
        std::shared_ptr<Timer> m_timer;                 // 空任务的定时器，让调度器保持调度
    };
}