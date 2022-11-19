#include "log.h"
#include "config.h"
#include "fiber.h"
#include "macro.h"

namespace dbspider
{
    static Logger::ptr g_logger = DBSPIDER_LOG_NAME("system");

    // 全局静态变量，用于生成协程id
    static std::atomic<uint64_t> s_fiber_id{0};

    // 全局静态变量，用于统计当前的协程数
    static std::atomic<uint64_t> s_fiber_count{0};

    // 当前线程正在运行的协程指针
    static thread_local Fiber *t_fiber = nullptr;

    // 当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行
    static thread_local Fiber::ptr t_threadFiber = nullptr;

    static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
        Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "Fiber stack size");

    // malloc栈内存分配器
    class MallocStackAllocator
    {
    public:
        static void *Alloc(size_t size) { return malloc(size); }
        static void Dealloc(void *vp, size_t size) { free(vp); }
    };

    using StackAllocator = MallocStackAllocator;

    // 主协程构造
    Fiber::Fiber()
    {
        m_state = EXEC;
        SetThis(this);
        if (getcontext(&m_ctx))
        {
            DBSPIDER_ASSERT2(false, "System error: getcontext fail");
        }
        ++s_fiber_count;
    }

    // 普通协程构造
    Fiber::Fiber(std::function<void()> cb, size_t stacksize)
        : m_id(++s_fiber_id),
          m_cb(cb)
    {
        DBSPIDER_ASSERT2(t_fiber, "Fiber error: no main fiber");

        s_fiber_count++;
        m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
        m_stack = StackAllocator::Alloc(m_stacksize);

        if (getcontext(&m_ctx))
        {
            DBSPIDER_ASSERT2(false, "System error: getcontext fail");
        }
        m_ctx.uc_stack.ss_size = m_stacksize;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_link = nullptr;

        makecontext(&m_ctx, MainFunc, 0);
        DBSPIDER_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
    }

    // 线程的主协程析构时需要特殊处理，因为主协程没有分配栈和cb
    Fiber::~Fiber()
    {
        --s_fiber_count;
        if (m_stack)
        {
            // 有栈，说明是子协程，需要确保子协程一定是结束状态
            DBSPIDER_ASSERT(m_state == INIT || m_state == TERM || m_state == EXCEPT);
            StackAllocator::Dealloc(m_stack, m_stacksize);
        }
        else
        {
            // 没有栈，说明是线程的主协程
            DBSPIDER_ASSERT(!m_cb);           // 主协程没有cb
            DBSPIDER_ASSERT(m_state == EXEC); // 主协程一定是执行状态
            if (t_fiber == this)              // 当前协程就是自己
            {
                SetThis(nullptr);
            }
        }
        DBSPIDER_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id
                                     << " total=" << s_fiber_count;
    }

    // 切换到本协程
    void Fiber::resume()
    {
        SetThis(this);
        DBSPIDER_ASSERT2(m_state != EXEC, "Fiber id=" + std::to_string(m_id));
        m_state = EXEC;

        // 保存当前上下文到主协程，切换到子协程上下文
        if (swapcontext(&(t_threadFiber->m_ctx), &m_ctx))
        {
            DBSPIDER_ASSERT2(false, "system error: swapcontext() fail");
        }
    }

    // 让出当前协程
    void Fiber::yield()
    {
        SetThis(t_threadFiber.get());

        // 保存子协程上下文，切换到主协程上下文
        if (swapcontext(&m_ctx, &(t_threadFiber->m_ctx)))
        {
            DBSPIDER_ASSERT2(false, "system error: swapcontext() fail");
        }
    }

    // 重置协程执行函数，并重置状态
    void Fiber::reset(std::function<void()> cb)
    {
        DBSPIDER_ASSERT(m_stack);
        DBSPIDER_ASSERT(m_state == INIT || m_state == TERM || m_state == EXCEPT);

        m_cb = cb;

        if (getcontext(&m_ctx))
        {
            DBSPIDER_ASSERT2(false, "System error: getcontext fail");
        }

        m_ctx.uc_stack.ss_size = m_stacksize;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_link = nullptr;

        makecontext(&m_ctx, MainFunc, 0);
        m_state = INIT;
    }

    // 在当前线程启用协程
    void Fiber::EnableFiber()
    {
        if (!t_fiber)
        {
            Fiber::ptr main_fiber(new Fiber);
            DBSPIDER_ASSERT(t_fiber == main_fiber.get());
            t_threadFiber = main_fiber;
        }
    }

    void Fiber::MainFunc()
    {
        Fiber::ptr cur = GetThis(); // GetThis() 的 shared_from_this() 方法让引用计数加1
        DBSPIDER_ASSERT(cur);
        try
        {
            cur->m_cb();
            cur->m_cb = nullptr;
            cur->m_state = TERM;
        }
        catch (std::exception &ex)
        {
            cur->m_state = EXCEPT;
            DBSPIDER_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what();
        }
        catch (...)
        {
            cur->m_state = EXCEPT;
            DBSPIDER_LOG_ERROR(g_logger) << "Fiber Except: ";
        }

        auto ptr = cur.get();
        cur = nullptr;
        ptr->yield();
    }

    // 让出协程，并设置协程状态为Ready
    void Fiber::YieldToHold()
    {
        Fiber::ptr cur = GetThis();
        cur->m_state = HOLD;
        cur->yield();
    }

    // 让出协程，并设置协程状态为READY
    void Fiber::YieldToReady()
    {
        Fiber::ptr cur = GetThis();
        cur->m_state = READY;
        cur->yield();
    }

    // 获取当前协程，同时充当初始化当前线程主协程的作用，这个函数在使用协程之前要调用一下
    Fiber::ptr Fiber::GetThis()
    {
        return t_fiber->shared_from_this();
    }

    // 获取当前协程Id
    uint64_t Fiber::GetFiberId()
    {
        if (t_fiber)
        {
            return t_fiber->getId();
        }
        return 0;
    }

    // 设置当前协程
    void Fiber::SetThis(Fiber *f)
    {
        t_fiber = f;
    }

    // 协程总数
    uint64_t Fiber::TotalFibers()
    {
        return s_fiber_count;
    }

}