#pragma once

#include <functional>
#include <memory>
#include <ucontext.h>

#include "thread.h"

namespace dbspider
{
    class Fiber : public std::enable_shared_from_this<Fiber>
    {
    public:
        using ptr = std::shared_ptr<Fiber>;

        enum State
        {
            INIT,
            HOLD,
            EXEC,
            TERM,
            READY,
            EXCEPT
        };

        Fiber(std::function<void()> cb, size_t stacksize = 0);
        ~Fiber();

        // 切换到当前协程
        void resume();

        // 让出当前协程
        // TODO:协程是否参与调度器调度
        void yield();

        // 重置协程执行函数，并重置状态
        void reset(std::function<void()> cb);

        // 获取协程ID
        uint64_t getId() const { return m_id; }
        // 获取协程状态
        State getState() const { return m_state; }

        bool isTerminate() const { return m_state == TERM || m_state == EXCEPT; }

        // 在当前线程启用协程
        static void EnableFiber();

        // 协程入口函数
        static void MainFunc();

        // 让出协程，并设置协程状态为Ready
        static void YieldToHold();

        // 让出协程，并设置协程状态为Hold
        static void YieldToReady();

        // 返回当前线程正在执行的协程
        // 如果当前线程还未创建协程，则创建线程的第一个协程，
        // 且该协程为当前线程的主协程，其他协程都通过这个协程来调度，也就是说，其他协程
        // 结束时,都要切回到主协程，由主协程重新选择新的协程进行resume
        // 线程如果要创建协程，那么应该首先执行一下Fiber::GetThis()操作，以初始化主函数协程
        static Fiber::ptr GetThis();

        // 获取当前协程Id
        static uint64_t GetFiberId();

        // 设置当前协程
        static void SetThis(Fiber *f);

        // 协程总数
        static uint64_t TotalFibers();

    private:
        // 构造函数
        // 无参构造函数只用于创建线程的第一个协程，也就是线程主函数对应的协程，
        // 这个协程只能由GetThis()方法调用，所以定义成私有方法
        Fiber();

    private:
        uint64_t m_id = 0;          // 协程id
        uint32_t m_stacksize = 0;   // 协程栈大小
        State m_state = INIT;       // 协程状态
        ucontext_t m_ctx;           // 协程上下文
        void *m_stack = nullptr;    // 协程栈指针
        std::function<void()> m_cb; // 协程运行的函数
    };
}