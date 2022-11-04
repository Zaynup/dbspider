#pragma once

#include <semaphore.h>
#include <pthread.h>
#include <atomic>
#include <memory>
#include <queue>

#include "../base/noncopyable.h"

namespace dbspider
{
    // RAII 锁包装
    template <typename T>
    class ScopedLock
    {
    public:
        ScopedLock(T &mutex) : m_mutex(mutex)
        {
            m_mutex.lock();
            m_locked = true;
        }

        ~ScopedLock()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked)
            {
                m_mutex.lock();
                m_locked = true;
            }
        }

        bool tryLock()
        {
            if (!m_locked)
            {
                m_locked = m_mutex.tryLock();
            }
            return m_locked;
        }

        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        T &m_mutex;
        bool m_locked = false;
    };

    // 自旋锁
    class SpinLock : Noncopyable
    {
    public:
        using Lock = ScopedLock<SpinLock>;

        SpinLock()
        {
            pthread_spin_init(&m_mutex, 0);
        }
        ~SpinLock()
        {
            pthread_spin_destroy(&m_mutex);
        }
        void lock()
        {
            pthread_spin_lock(&m_mutex);
        }
        bool tryLock()
        {
            return !pthread_spin_trylock(&m_mutex);
        }
        void unlock()
        {
            pthread_spin_unlock(&m_mutex);
        }

    private:
        pthread_spinlock_t m_mutex;
    };

    // 互斥锁
    class Mutex : Noncopyable
    {
    public:
        using Lock = ScopedLock<Mutex>;
        Mutex()
        {
            pthread_mutex_init(&m_mutex, nullptr);
        }
        ~Mutex()
        {
            pthread_mutex_destroy(&m_mutex);
        }
        void lock()
        {
            pthread_mutex_lock(&m_mutex);
        }
        bool tryLock()
        {
            return !pthread_mutex_trylock(&m_mutex);
        }
        void unlock()
        {
            pthread_mutex_unlock(&m_mutex);
        }

    private:
        pthread_mutex_t m_mutex;
    };

    // 读锁RAII包装
    template <typename T>
    class ReadScopedLock
    {
    public:
        ReadScopedLock(T &mutex) : m_mutex(mutex)
        {
            m_mutex.rlock();
            m_locked = true;
        }

        ~ReadScopedLock()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked)
            {
                m_mutex.rlock();
                m_locked = true;
            }
        }

        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        T &m_mutex;
        bool m_locked = false;
    };

    // 写锁RAII包装
    template <class T>
    class WriteScopedLock
    {
    public:
        WriteScopedLock(T &mutex) : m_mutex(mutex)
        {
            m_mutex.wlock();
            m_locked = true;
        }

        ~WriteScopedLock()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked)
            {
                m_mutex.wlock();
                m_locked = true;
            }
        }

        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        T &m_mutex;
        bool m_locked = false;
    };

    // 读写锁
    class RWMutex : Noncopyable
    {
    public:
        using ReadLock = ReadScopedLock<RWMutex>;
        using WriteLock = WriteScopedLock<RWMutex>;

        RWMutex()
        {
            pthread_rwlock_init(&m_rwmutex, nullptr);
        }

        ~RWMutex()
        {
            pthread_rwlock_destroy(&m_rwmutex);
        }

        void rlock()
        {
            pthread_rwlock_rdlock(&m_rwmutex);
        }

        void wlock()
        {
            pthread_rwlock_wrlock(&m_rwmutex);
        }

        void unlock()
        {
            pthread_rwlock_unlock(&m_rwmutex);
        }

    private:
        pthread_rwlock_t m_rwmutex;
    };

    // 协程锁
    class Fiber;
    class CoMutex : Noncopyable
    {
    public:
        using Lock = ScopedLock<CoMutex>;

        bool tryLock();

        void lock();

        void unlock();

    private:
        SpinLock m_mutex;                               // 协程所持有的锁
        SpinLock m_gaurd;                               // 保护等待队列的锁
        uint64_t m_fiberId = 0;                         // 持有锁的协程id
        std::queue<std::shared_ptr<Fiber>> m_waitQueue; // 协程等待队列
    };

}