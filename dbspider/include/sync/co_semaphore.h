#pragma once
#include "co_condvar.h"

namespace dbspider
{
    /**
     * @brief 协程信号量
     */
    class CoSemaphore : Noncopyable
    {
    public:
        CoSemaphore(uint32_t num)
        {
            m_num = num;
            m_used = 0;
        }

        void wait()
        {
            CoMutex::Lock lock(m_mutex);
            // 如果已经获取的信号量大于等于信号量数量则让出协程等待
            while (m_used >= m_num)
            {
                m_condvar.wait(lock);
            }
            ++m_used;
        }

        void notify()
        {
            CoMutex::Lock lock(m_mutex);
            if (m_used > 0)
            {
                --m_used;
            }
            // 通知一个等待的协程
            m_condvar.notify();
        }

    private:
        uint32_t m_num;      // 信号量的数量
        uint32_t m_used;     // 已经获取的信号量的数量
        CoCondvar m_condvar; // 协程条件变量
        CoMutex m_mutex;     // 协程锁
    };

    class Semaphore : Noncopyable
    {
    public:
        Semaphore(uint32_t count)
        {
            if (sem_init(&m_sem, 0, count))
            {
                throw std::logic_error("sem_init error");
            }
        }

        ~Semaphore()
        {
            sem_destroy(&m_sem);
        }

        void wait()
        {
            if (sem_wait(&m_sem))
            {
                throw std::logic_error("sem_wait error");
            }
        }

        void notify()
        {
            if (sem_post(&m_sem))
            {
                throw std::logic_error("sem_notify error");
            }
        }

    private:
        sem_t m_sem;
    };
}
