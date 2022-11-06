#include "timer.h"
#include "util.h"

namespace dbspider
{

    Timer::Timer(uint64_t next)
        : m_next(next)
    {
    }

    Timer::Timer(uint64_t ms,
                 std::function<void()> cb,
                 bool recurring,
                 TimeManager *timeManager)
        : m_ms(ms),
          m_cb(cb),
          m_recurring(recurring),
          m_manager(timeManager)
    {
        m_next = GetCurrentMS() + m_ms;
    }

    // 取消定时器
    bool Timer::cancel()
    {
        TimeManager::MutexType::Lock lock(m_manager->m_mutex);
        if (m_cb)
        {
            auto it = m_manager->m_timers.find(shared_from_this());
            m_manager->m_timers.erase(it);
            return true;
        }
        return false;
    }

    // 刷新设置定时器的执行时间
    bool Timer::refresh()
    {
        TimeManager::MutexType::Lock lock(m_manager->m_mutex);
        if (!m_cb)
        {
            return false;
        }
        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end())
        {
            return false;
        }
        m_manager->m_timers.erase(it);
        m_next = GetCurrentMS() + m_ms;
        m_manager->m_timers.insert(shared_from_this());

        return false;
    }

    // 重置定时器时间
    bool Timer::reset(uint64_t ms, bool now_time)
    {
        if (ms == m_ms && now_time == false)
        {
            return true;
        }
        TimeManager::MutexType::Lock lock(m_manager->m_mutex);
        if (!m_cb)
        {
            return false;
        }

        auto it = m_manager->m_timers.find(shared_from_this());
        if (it != m_manager->m_timers.end())
        {
            m_manager->m_timers.erase(it);
        }
        uint64_t start = 0;
        if (now_time)
        {
            start = dbspider::GetCurrentMS();
        }
        else
        {
            start = m_next - m_ms;
        }
        m_ms = ms;
        m_next = start + m_ms;
        m_manager->addTimer(shared_from_this(), lock);

        return true;
    }

    bool Timer::Compare::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const
    {
        if (!lhs && !rhs)
        {
            return false;
        }
        if (!lhs)
        {
            return true;
        }
        if (!rhs)
        {
            return false;
        }
        if (lhs->m_next < rhs->m_next)
        {
            return true;
        }
        if (rhs->m_next < lhs->m_next)
        {
            return false;
        }
        return lhs.get() < rhs.get();
    }

    TimeManager::TimeManager()
    {
    }

    TimeManager::~TimeManager()
    {
    }

    // 添加定时器
    Timer::ptr TimeManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
    {
        Timer::ptr timer(new Timer(ms, cb, recurring, this));
        MutexType::Lock lock(m_mutex);
        addTimer(timer, lock);
        return timer;
    }

    // 添加条件定时器
    Timer::ptr TimeManager::addConditionTimer(uint64_t ms,
                                              std::function<void()> cb,
                                              std::weak_ptr<void> cond,
                                              bool recurring)
    {
        return addTimer(
            ms,
            [cb, cond]()
            {
                std::shared_ptr<void> tmp = cond.lock();
                if (tmp)
                {
                    cb();
                }
            },
            recurring);
    }

    // 到最近一个定时器执行的时间间隔(ms)
    uint64_t TimeManager::getNextTimer()
    {
        MutexType::Lock lock(m_mutex);
        if (m_timers.empty())
        {
            return ~0ull;
        }
        m_tickled = false;
        Timer::ptr timer = *m_timers.begin();
        uint64_t now = GetCurrentMS();
        if (now >= timer->m_next)
        {
            return 0;
        }
        else
        {
            return timer->m_next - now;
        }
    }

    // 获取需要执行的定时器的回调函数列表
    void TimeManager::getExpiredCallbacks(std::vector<std::function<void()>> &cbs)
    {
        uint64_t now = GetCurrentMS();
        std::vector<Timer::ptr> expired;

        MutexType::Lock lock(m_mutex);
        if (m_timers.empty())
        {
            return;
        }
        Timer::ptr timer(new Timer(now));

        auto it = m_timers.lower_bound(timer);
        while (it != m_timers.end() && (*it)->m_next == now)
        {
            ++it;
        }
        expired.insert(expired.begin(), m_timers.begin(), it);
        m_timers.erase(m_timers.begin(), it);

        cbs.reserve(expired.size());

        for (auto &i : expired)
        {
            cbs.push_back(i->m_cb);
            if (i->m_recurring)
            {
                i->m_next = now + i->m_ms;
                m_timers.insert(i);
            }
        }
    }

    // 是否有定时器
    bool TimeManager::hasTimer()
    {
        MutexType::Lock lock(m_mutex);
        return m_timers.size();
    }

    // 将定时器添加到管理器中
    void TimeManager::addTimer(Timer::ptr timer, MutexType::Lock &lock)
    {
        auto it = m_timers.insert(timer).first;
        bool at_front = (it == m_timers.begin()) && !m_tickled;
        if (at_front)
        {
            m_tickled = true;
        }
        if (at_front)
        {
            onInsertAtFront();
        }
    }
}