#include "fiber.h"
#include "io_manager.h"
#include "co_condvar.h"

namespace dbspider
{
    void CoCondVar::notify()
    {
        Fiber::ptr fiber;
        {
            // 获取一个等待的协程
            MutexType::Lock lock(m_mutex);
            while (m_waitQueue.size())
            {
                fiber = *m_waitQueue.begin();
                m_waitQueue.erase(m_waitQueue.begin());
                if (fiber)
                {
                    break;
                }
            }
            if (m_timer)
            {
                // 删除定时器
                m_timer->cancel();
                m_timer = nullptr;
            }
        }
        // 将协程重新加入调度
        if (fiber)
        {
            go fiber;
        }
    }

    void CoCondVar::notifyAll()
    {
        MutexType::Lock lock(m_mutex);
        // 将全部等待的协程重新加入调度
        while (m_waitQueue.size())
        {
            Fiber::ptr fiber = *m_waitQueue.begin();
            m_waitQueue.erase(m_waitQueue.begin());
            if (fiber)
            {
                go fiber;
            }
        }
        // 删除定时器
        if (m_timer)
        {
            m_timer->cancel();
            m_timer = nullptr;
        }
    }

    void CoCondVar::wait()
    {
        Fiber::ptr self = Fiber::GetThis();
        {
            MutexType::Lock lock(m_mutex);
            // 将自己加入等待队列
            m_waitQueue.insert(self);
            if (!m_timer)
            {
                // 加入一个空任务定时器，不让调度器退出
                m_timer = IOManager::GetThis()->addTimer(
                    UINT32_MAX, [] {}, true);
            }
        }
        // 让出协程
        Fiber::YieldToHold();
    }

    // 注意：协程锁解锁了才能加入到等待队列，否则别的协程无法获取锁，被唤醒后要重新获取锁
    void CoCondVar::wait(CoMutex::Lock &lock)
    {
        Fiber::ptr self = Fiber::GetThis();
        {
            MutexType::Lock lock1(m_mutex);
            // 将自己加入等待队列
            m_waitQueue.insert(self);
            if (!m_timer)
            {
                // 加入一个空任务定时器，不让调度器退出
                m_timer = IOManager::GetThis()->addTimer(
                    UINT32_MAX, [] {}, true);
            }
            // 先解锁
            lock.unlock();
        }

        // 让出协程
        Fiber::YieldToHold();
        // 重新获取锁
        lock.lock();
    }

    bool CoCondVar::waitFor(CoMutex::Lock &lock, uint64_t timeout_ms)
    {
        if (timeout_ms == (uint64_t)-1)
        {
            wait(lock);
            return true;
        }
        Fiber::ptr self = Fiber::GetThis();
        IOManager *ioManager = dbspider::IOManager::GetThis();
        std::shared_ptr<bool> timeCondition(new bool{false});
        std::weak_ptr<bool> weakPtr(timeCondition);
        Timer::ptr timer;
        {
            MutexType::Lock lock1(m_mutex);
            // 将自己加入等待队列
            m_waitQueue.insert(self);
            // 先解锁
            lock.unlock();
            timer = IOManager::GetThis()->addConditionTimer(
                timeout_ms,
                [weakPtr, ioManager, self, this]() mutable
                {
                    MutexType::Lock lock(m_mutex);
                    auto t = weakPtr.lock();
                    if (!t)
                    {
                        return;
                    }
                    *t = true;
                    m_waitQueue.erase(self);
                    ioManager->submit(self);
                },
                weakPtr);
        }
        // 让出协程
        Fiber::YieldToHold();
        {
            MutexType::Lock lock1(m_mutex);
            if (timer && !(*timeCondition))
            {
                timer->cancel();
            }
            // 重新获取锁
            lock.lock();
            if (*timeCondition)
            {
                // 超时
                return false;
            }
            return true;
        }
    }
}