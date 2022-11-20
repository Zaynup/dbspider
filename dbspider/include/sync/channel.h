#pragma once

#include <queue>
#include "co_condvar.h"

namespace dbspider
{
    // Channel 的具体实现
    template <typename T>
    class ChannelImpl : Noncopyable
    {
    public:
        ChannelImpl(size_t capacity)
            : m_isClose(false),
              m_capacity(capacity)
        {
        }

        ~ChannelImpl() { close(); }

        // 发送数据到 Channel
        bool push(const T &t)
        {
            CoMutex::Lock lock(m_mutex);
            if (m_isClose)
            {
                return false;
            }
            // 如果缓冲区已满，等待m_pushCv唤醒
            while (m_queue.size() >= m_capacity)
            {
                m_pushCv.wait(lock);
                if (m_isClose)
                {
                    return false;
                }
            }
            m_queue.push(t);
            // 唤醒 m_popCv
            m_popCv.notify();
            return true;
        }

        // 从 Channel 读取数据
        bool pop(T &t)
        {
            CoMutex::Lock lock(m_mutex);
            if (m_isClose)
            {
                return false;
            }
            // 如果缓冲区为空，等待m_pushCv唤醒
            while (m_queue.empty())
            {
                m_popCv.wait(lock);
                if (m_isClose)
                {
                    return false;
                }
            }
            t = m_queue.front();
            m_queue.pop();
            // 唤醒 m_pushCv
            m_pushCv.notify();
            return true;
        }

        // 从 Channel 读取数据，最多阻塞指定的 timeout
        // timeout 等待时间 (ms)
        // t 读取的结果
        // true 表示读取到数据; false 表示等待超时
        bool waitFor(T &t, uint64_t timeout_ms)
        {
            CoMutex::Lock lock(m_mutex);
            if (m_isClose)
            {
                return false;
            }
            // 如果缓冲区为空，等待m_pushCv唤醒
            while (m_queue.empty())
            {
                if (!m_popCv.waitFor(lock, timeout_ms))
                {
                    return false;
                }
                if (m_isClose)
                {
                    return false;
                }
            }
            t = m_queue.front();
            m_queue.pop();
            // 唤醒 m_pushCv
            m_pushCv.notify();
            return true;
        }

        ChannelImpl &operator>>(T &t)
        {
            pop(t);
            return *this;
        }

        ChannelImpl &operator<<(const T &t)
        {
            push(t);
            return *this;
        }

        // 关闭 Channel
        void close()
        {
            CoMutex::Lock lock(m_mutex);
            if (m_isClose)
            {
                return;
            }
            m_isClose = true;
            // 唤醒等待的协程
            m_pushCv.notify();
            m_popCv.notify();
            std::queue<T> q;
            std::swap(m_queue, q);
        }

        operator bool() { return !m_isClose; }

        size_t capacity() const { return m_capacity; }

        size_t size()
        {
            CoMutex::Lock lock(m_mutex);
            return m_queue.size();
        }

        bool empty() { return !size(); }

    private:
        bool m_isClose;

        size_t m_capacity;     // Channel 缓冲区大小
        CoMutex m_mutex;       // 协程锁和协程条件变量配合使用保护消息队列
        CoCondVar m_pushCv;    // 入队条件变量
        CoCondVar m_popCv;     // 出队条件变量
        std::queue<T> m_queue; // 消息队列
    };

    /**
     * Channel主要是用于协程之间的通信，属于更高级层次的抽象
     * 在类的实现上采用了 PIMPL 设计模式，将具体操作转发给实现类
     * Channel 对象可随意复制，通过智能指针指向同一个 ChannelImpl
     */
    template <typename T>
    class Channel
    {
    public:
        Channel(size_t capacity) { m_channel = std::make_shared<ChannelImpl<T>>(capacity); }
        Channel(const Channel &chan) { m_channel = chan.m_channel; }

        bool push(const T &t) { return m_channel->push(t); }
        bool pop(T &t) { return m_channel->pop(t); }
        void close() { m_channel->close(); }

        size_t capacity() const { return m_channel->capacity(); }
        size_t size() { return m_channel->size(); }
        bool empty() { return m_channel->empty(); }
        bool unique() const { return m_channel.unique(); }

        operator bool() const { return *m_channel; }

        // 从 Channel 读取数据，最多阻塞指定的 timeout
        // timeout 等待时间 (ms)
        // t 读取的结果
        // true 表示读取到数据; false 表示等待超时
        bool waitFor(T &t, uint64_t timeout_ms)
        {
            return m_channel->waitFor(t, timeout_ms);
        }

        Channel &operator>>(T &t)
        {
            (*m_channel) >> t;
            return *this;
        }

        Channel &operator<<(const T &t)
        {
            (*m_channel) << t;
            return *this;
        }

    private:
        std::shared_ptr<ChannelImpl<T>> m_channel;
    };

}
