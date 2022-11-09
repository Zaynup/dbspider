#pragma once

#include "co_condvar.h"

namespace dbspider
{
    // 协程计数器
    class CoCountDownLatch
    {
    public:
        CoCountDownLatch(int count)
            : m_count(count)
        {
        }

        // 使当前协程挂起等待，直到count的值被减到0，当前线程就会被唤醒。
        void wait()
        {
            CoMutex::Lock lock(m_mutex);
            if (!m_count)
            {
                return;
            }
            m_condvar.wait();
        }

        // 使latch的值减1，如果减到了0，则会唤醒所有等待在这个latch上的线程。
        bool countDown()
        {
            CoMutex::Lock lock(m_mutex);
            if (!m_count)
            {
                return false;
            }
            m_count--;
            if (!m_count)
            {
                m_condvar.notifyAll();
            }
            return true;
        }

        int getCount()
        {
            CoMutex::Lock lock(m_mutex);
            return m_count;
        }

    private:
        int m_count;
        CoMutex m_mutex;
        CoCondVar m_condvar;
    };
}
