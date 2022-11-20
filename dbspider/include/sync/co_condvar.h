#pragma once

#include "noncopyable.h"
#include "mutex.h"
#include <set>

namespace dbspider
{
    class Fiber;
    class Timer;

    // 协程条件变量
    class CoCondVar : Noncopyable
    {
    public:
        using MutexType = SpinLock;

        // 唤醒一个等待的协程
        void notify();

        // 唤醒全部等待的协程
        void notifyAll();

        // 不加锁地等待唤醒
        void wait();

        // 等待唤醒
        void wait(CoMutex::Lock &lock);

        // 阻塞当前协程，直到条件变量被唤醒，或到指定时限时长后
        // timeout_ms 超时时间 (ms)
        // 返回值：true 表示被条件变量唤醒; false 表示超时唤醒
        bool waitFor(CoMutex::Lock &lock, uint64_t timeout_ms);

    private:
        std::set<std::shared_ptr<Fiber>> m_waitQueue; // 协程等待队列
        MutexType m_mutex;                            // 保护协程等待队列
        std::shared_ptr<Timer> m_timer;               // 空任务的定时器，让调度器保持调度
    };
}