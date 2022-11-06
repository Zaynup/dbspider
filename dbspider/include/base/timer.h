#pragma once

#include <functional>
#include <memory>
#include <set>

#include "log.h"
#include "sync.h"

namespace dbspider
{
    class TimeManager;

    // 定时器
    class Timer : public std::enable_shared_from_this<Timer>
    {
        friend class TimeManager;

    public:
        using ptr = std::shared_ptr<Timer>;

        // 取消定时器
        bool cancel();

        // 刷新设置定时器的执行时间
        bool refresh();

        // 重置定时器时间
        bool reset(uint64_t ms, bool now_time);

    private:
        Timer(uint64_t next);
        Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimeManager *timeManager);

        // 定时器比较仿函数
        struct Compare
        {
            // 比较定时器的智能指针的大小(按执行时间排序)
            bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
        };

    private:
        uint64_t m_ms = 0;                // 执行周期
        uint64_t m_next = 0;              // 精确的执行时间
        std::function<void()> m_cb;       // 定时器回调
        bool m_recurring = false;         // 是否循环
        TimeManager *m_manager = nullptr; // 定时器管理类
    };

    // 定时器管理器Timer
    class TimeManager
    {
        friend class Timer;

    public:
        using MutexType = Mutex;

        TimeManager();
        virtual ~TimeManager();

        // 添加定时器
        Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

        // 添加条件定时器
        Timer::ptr addConditionTimer(uint64_t ms,
                                     std::function<void()> cb,
                                     std::weak_ptr<void> cond,
                                     bool recurring = false);

        // 到最近一个定时器执行的时间间隔(ms)
        uint64_t getNextTimer();

        // 获取需要执行的定时器的回调函数列表
        void getExpiredCallbacks(std::vector<std::function<void()>> &cbs);

        // 是否有定时器
        bool hasTimer();

    protected:
        // 当有新的定时器插入到定时器的首部，执行该函数
        virtual void onInsertAtFront() = 0;

        // 将定时器添加到管理器中
        void addTimer(Timer::ptr timer, MutexType::Lock &lock);

    private:
        MutexType m_mutex;
        std::set<Timer::ptr, Timer::Compare> m_timers; // 定时器集合
        bool m_tickled = false;                        // 是否触发onTimerInsertedAtFront
    };
}