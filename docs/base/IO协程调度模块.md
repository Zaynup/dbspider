# IO协程调度模块

## 1. IO协程调度模块概述

+ IO协程调度可以看成是增强版的协程调度。
+ IO协程调度支持协程调度的全部功能，因为IO协程调度器是直接继承协程调度器实现的。
+ 除了协程调度，IO协程调度增加了 **IO事件调度** 的功能，这个功能是针对 **描述符** （一般是 **套接字描述符** ）的。IO协程调度支持为描述符 **注册可读和可写事件** 的回调函数，当描述符可读或可写时，执行对应的回调函数。（回调函数可以等效成协程）
  + IO事件调度功能对服务器开发至关重要，因为服务器通常需要处理大量来自客户端的 `socket fd` ，使用IO事件调度可以将开发者从判断  `socket fd` 是否可读或可写的工作中解放出来，使得程序员只需要关心 `socket fd` 的IO操作。
+ 在前面协程调度模块中，调度器对协程的调度是无条件执行的，在启动调度的情况下，任务一旦添加成功，就会排队等待调度器执行。不支持删除调度任务，并且调度器在正常退出之前一定会执行完全部的调度任务，所以在某种程度上可以认为，把一个协程添加到调度器的任务队列，就相当于调用了协程的resume方法。
+ IO协程调度解决了调度器在 `idle`(`wait`) 状态下忙等待导致CPU占用率高的问题。
  + IO协程调度器使用一对 **管道fd** 来 `notify` 调度协程，当调度器空闲时， `wait` 协程通过 `epoll_wait` 阻塞在管道的读描述符上，等管道的可读事件。添加新任务时， `notify` 方法写管道， `wait` 协程检测到管道可读后退出，调度器执行调度。

## 2. IO协程调度模块设计

### 2.1 IO事件

+ IO协程调度模块基于 **epoll** 实现。对每个 `fd` ，支持两类事件，一类是**可读事件**，对应 `EPOLLIN` ，一类是**可写事件**，对应 `EPOLLOUT` ，事件枚举值直接继承自 `epoll` 。

    ```C++
    // IO事件，继承自 epoll 对事件的定义
    // 这里只关心 socket fd 的读和写事件，其他 epoll 事件会归类到这两类事件中
    enum Event
    {
        NONE = 0x0, // 无事件
        READ = 0x1, // 读事件(EPOLLIN)
        WRITE = 0x4 // 写事件(EPOLLOUT)
    };
    ```

+ 当然 `epoll` 本身除了支持了 `EPOLLIN` 和 `EPOLLOUT` 两类事件外，还支持其他事件，比如 `EPOLLRDHUP` ,  `EPOLLERR` , `EPOLLHUP` 等，对于这些事件，框架的做法是将其进行归类，分别对应到 `EPOLLIN` 和 `EPOLLOUT` 中，也就是所有的事件都可以表示为可读或可写事件，甚至有的事件还可以同时表示可读及可写事件，比如 `EPOLLERR` `事件发生时，fd` 将同时触发可读和可写事件。

### 2.2 fd上下文封装

+ 对于IO协程调度来说，每次调度都包含一个三元组信息，分别是**描述符-事件类型-回调函数**，调度器记录全部需要调度的三元组信息，其中
  + 描述符和事件类型用于 `epoll_wait`
  + 回调函数用于协程调度
+ 这个三元组信息在源码上通过 `FdContext` 结构体来存储，在执行 `epoll_wait` 时通过 `epoll_event` 的私有数据指针 `data.ptr` 来保存 `FdContext` 结构体信息。

    ```C++
    // Socket事件上下文类
    struct FdContext
    {
        using MutexType = Mutex;

        // 事件上下文类
        // fd的每个事件都有一个事件上下文，保存这个事件的回调函数以及执行回调函数的调度器
        struct EventContext
        {
            Scheduler *scheduler = nullptr; // 事件执行的调度器
            Fiber::ptr fiber;               // 事件的协程
            std::function<void()> cb;       // 事件的回调函数

            bool empty()
            {
                return !scheduler && !fiber && !cb;
            }
        };

        // 获取事件上下文类
        EventContext &getContext(Event event);

        // 重置事件上下文
        void resetContext(EventContext &event);

        // 触发事件
        // 根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
        void triggerEvent(Event event);

        int fd = 0;          // 事件关联句柄
        EventContext read;   // 读事件上下文
        EventContext write;  // 写事件上下文
        Event events = NONE; // 注册的事件类型
        MutexType mutex;     // 事件的Mutex
    };
    ```

### 2.3 成员变量

+ `IOManager` 包含一个 `epoll` 实例的句柄 `m_epfd` 以及用于 `tickle` 的一对 `pipe fd` ，还有全部的 `fd` 上下文 `m_fdContexts` 。

    ```C++
    int m_epfd;                                    // epoll 文件句柄
    int m_tickleFds[2];                            // pipe 文件句柄，fd[0]读端，fd[1]写端
    std::atomic<size_t> m_pendingEventCount = {0}; // 当前等待执行的事件数量
    RWMutexType m_mutex;                           // IOManager的mutex
    std::vector<FdContext *> m_fdContexts;         // socket事件上下文的容器
    ```

### 2.4 协程调度器构造与通知调度

+ 继承类 `IOManager` 中改造协程调度器，使其支持 `epoll` ，并重载 `notify` 和 `wait` ，实现通知调度协程和IO协程调度功能：

    ```C++
    IOManager::IOManager(size_t threads, const std::string &name)
            : Scheduler(threads, name)
    {
        // 创建pipe，获取m_tickleFds[2]，其中m_tickleFds[0]是管道的读端，m_tickleFds[1]是管道的写端
        int rt = pipe(m_tickleFds);
        DBSPIDER_ASSERT(!rt);

        // 创建epoll实例
        m_epfd = epoll_create(1);
        DBSPIDER_ASSERT(m_epfd > 0);

        // 注册pipe读句柄的可读事件，用于notify调度协程，通过epoll_event.data.fd保存描述符
        epoll_event event;
        memset(&event, 0, sizeof(epoll_event));
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = m_tickleFds[0];

        // 非阻塞方式，配合边缘触发
        rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
        DBSPIDER_ASSERT(!rt);

        // 将管道的读描述符加入epoll多路复用，如果管道可读，wait中的epoll_wait会返回
        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
        DBSPIDER_ASSERT(!rt);

        contextResize(64);

        // 这里直接开启了Schedluer，也就是说IOManager创建即可调度协程
        start();
    }
    ```

+ **notify()** 通知调度器有任务要调度
+ 写 pipe 让 wait 协程从 epoll_wait 退出，待 wait 协程 yield 之后 Scheduler::run 就可以调度其他任务如果当前没有空闲调度线程，那就没必要发通知。

    ```C++
    void IOManager::notify()
    {
        // 没有空闲线程返回
        if (!hasIdleThreads())
        {
            return;
        }
        int rt = write(m_tickleFds[1], "N", 1);
        DBSPIDER_ASSERT(rt == 1);
    }
    ```

+ **wait 协程**
+ 对于IO协程调度来说，应阻塞在等待IO事件上， `wait` 退出的时机是 `epoll_wait` 返回，对应的操作是 `notify` 或注册的IO事件就绪
调度器无调度任务时会阻塞 `wait` 协程上，对IO调度器而言， `wait` 状态应该关注两件事，
  + 一是有没有新的调度任务，对应 `Schduler::submit()` ，如果有新的调度任务，那应该立即退出 `wait` 状态，并执行对应的任务；
  + 二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行IO事件对应的回调函数

    ```C++
    void IOManager::wait()
    {
        DBSPIDER_LOG_DEBUG(g_logger) << "wait for event";

        const uint64_t MAX_EVNETS = 256;   // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理

        epoll_event *events = new epoll_event[MAX_EVNETS]();
        std::unique_ptr<epoll_event[]> uniquePtr(events);
        while (true)
        {
            uint64_t next_timeout = 0;
            if (stopping(next_timeout))
            {
                DBSPIDER_LOG_INFO(g_logger) << "name=" << getName()
                                            << " idle stopping exit";
                return;
            }
            int rt = 0;
            do
            {
                static const int MAX_TIMEOUT = 3000;
                if (next_timeout != ~0ull)
                {
                    next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
                }
                else
                {
                    next_timeout = MAX_TIMEOUT;
                }
                // 阻塞在epoll_wait上，等待事件发生
                rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
                if (rt < 0 && errno == EINTR)
                {
                    continue;
                }
                else
                {
                    break;
                }
            } while (true);

            std::vector<std::function<void()>> cbs;
            getExpiredCallbacks(cbs);
            if (cbs.size())
            {
                submit(cbs.begin(), cbs.end());
            }

            // 遍历所有发生的事件，根据 epoll_event 的私有指针找到对应的 FdContext ，进行事件处理
            for (int i = 0; i < rt; ++i)
            {
                epoll_event &event = events[i];
                if (event.data.fd == m_tickleFds[0])
                {
                    uint8_t dummy[256];
                    // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可，本轮 wait 结束Scheduler::run会重新执行协程调度
                    while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                        ;
                    continue;
                }

                // 通过epoll_event的私有指针获取FdContext
                FdContext *fdContext = static_cast<FdContext *>(event.data.ptr);
                FdContext::MutexType::Lock lock(fdContext->mutex);

                // EPOLLERR: 出错，比如写读端已经关闭的pipe
                // EPOLLHUP: 套接字对端关闭
                // 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
                if (event.events & (EPOLLERR | EPOLLHUP))
                {
                    event.events |= ((EPOLLIN | EPOLLOUT) & fdContext->events);
                }
                int real_events = NONE;
                if (event.events & EPOLLIN)
                {
                    real_events |= READ;
                }
                if (event.events & EPOLLOUT)
                {
                    real_events |= WRITE;
                }
                if ((real_events & fdContext->events) == NONE)
                {
                    continue;
                }

                // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait，
                // 如果剩下的事件为0，表示这个fd已经不需要关注了，直接从epoll中删除
                int left_events = fdContext->events & ~real_events;
                int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                event.events = left_events | EPOLLET;
                int res = epoll_ctl(m_epfd, op, fdContext->fd, &event);
                if (res)
                {
                    DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fdContext->fd << ", "
                                                    << event.events << "):" << res << " (" << errno << ") (" << strerror(errno) << ")";
                    continue;
                }
                // 处理已经发生的事件，也就是让调度器调度指定的函数或协程
                if (real_events & READ)
                {
                    fdContext->triggerEvent(READ);
                    --m_pendingEventCount;
                }
                if (real_events & WRITE)
                {
                    fdContext->triggerEvent(WRITE);
                    --m_pendingEventCount;
                }
            }

            // 一旦处理完所有的事件， wait 协程 yield ，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
            // 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还要等 wait 协程退出
            Fiber::YieldToHold();
        }
    }
    ```

### 2.5 事件回调

+ 注册事件回调 addEvent
+ 删除事件回调 delEvent
+ 取消事件回调 cancelEvent
+ 取消全部事件 cancelAll：

#### 2.5.1 注册事件回调

+ （1）找到 `fd` 对应的 `FdContext，若不存在就分配一个`
+ （2）将新的事件加入 `epoll_wait` ，使用 `epoll_event` 的私有指针存储 `FdContext` 的位置
+ （3）为 `fd` 的 `event` 事件对应的 `EventContext` 其中的 `scheduler` , `cb` , `fiber` 进行赋值

```C++
// 添加事件
bool IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    FdContext *fdContext = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd)
    {
        fdContext = m_fdContexts[fd];
        lock.unlock();
    }
    else
    {
        lock.unlock();
        RWMutexType::WriteLock lock1(m_mutex);
        contextResize(fd * 1.5);
        fdContext = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fdContext->mutex);
    if (fdContext->events & event)
    {
        DBSPIDER_LOG_ERROR(g_logger) << "fd=" << fd << " addEvent fail, event already register. "
                                        << "event=" << event << " FdContext->event=" << fdContext->events;
        DBSPIDER_ASSERT(!(fdContext->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    int op = fdContext->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    Event newEvent = (Event)(event | fdContext->events);
    epoll_event epevent;
    memset(&epevent, 0, sizeof(epoll_event));
    epevent.events = EPOLLET | newEvent;
    epevent.data.ptr = fdContext;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fd << ", "
                                        << epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 待执行IO事件数加1
    m_pendingEventCount++;

    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fdContext->events = newEvent;
    FdContext::EventContext &eventContext = fdContext->getContext(event);
    DBSPIDER_ASSERT(eventContext.empty());
    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    eventContext.scheduler = Scheduler::GetThis();
    if (cb)
    {
        eventContext.cb.swap(cb);
    }
    else
    {
        eventContext.fiber = Fiber::GetThis();
        DBSPIDER_ASSERT(eventContext.fiber->getState() == Fiber::EXEC);
    }

    return true;
}

```

#### 2.5.2 取消事件回调

+ (1) 找到要删除事件的 `fd`
+ (2) 清除指定的事件，如果清除之后结果为0，则从 `epoll_wait` 中删除该文件描述符
+ (3) 重置该 `fd` 对应的 `event` 事件上下文

```C++
// 删除事件
bool IOManager::delEvent(int fd, Event event)
{
    // 找到要删除的fd
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd)
    {
        return false;
    }
    FdContext *fdContext = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock1(fdContext->mutex);
    if (!(fdContext->events & event))
    {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event newEvents = (Event)(fdContext->events & ~event);
    int op = fdContext->events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    memset(&epevent, 0, sizeof(epoll_event));
    epevent.events = EPOLLET | newEvents;
    epevent.data.ptr = fdContext;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fd << ", "
                                        << epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 待执行事件数减1
    m_pendingEventCount--;

    // 重置该fd对应的event事件上下文
    fdContext->events = newEvents;
    FdContext::EventContext &eventContext = fdContext->getContext(event);
    fdContext->resetContext(eventContext);

    return true;
}

```

#### 2.5.3 取消事件回调

+ (1) 找到要取消事件的 `fd`
+ (2) 清除指定的事件，如果清除之后结果为0，则从 `epoll_wait` 中删除该文件描述符
+ (3) 删除之前触发一次事件

```C++

// 取消事件
bool IOManager::cancelEvent(int fd, Event event)
{
    RWMutexType::ReadLock lock(m_mutex);
    // 找到fd对应的FdContext
    if ((int)m_fdContexts.size() <= fd)
    {
        return false;
    }
    FdContext *fdContext = m_fdContexts[fd];
    lock.unlock();
    FdContext::MutexType::Lock lock1(fdContext->mutex);
    if (!(fdContext->events & event))
    {
        return false;
    }

    // 删除事件
    Event newEvents = (Event)(fdContext->events & ~event);
    int op = fdContext->events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    memset(&epevent, 0, sizeof(epoll_event));
    epevent.events = EPOLLET | newEvents;
    epevent.data.ptr = fdContext;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fd << ", "
                                        << epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }
    // 删除之前触发一次事件
    fdContext->triggerEvent(event);

    // 活跃事件数减1
    m_pendingEventCount--;

    return true;
}
```

#### 2.5.4 取消所有事件回调

+ (1) 找到要取消所有事件的 `fd`
+ (2) 删除全部事件
+ (3) 触发全部已注册的事件

```C++

// 取消所有事件
bool IOManager::cancelAllEvent(int fd)
{
    RWMutexType::ReadLock lock(m_mutex);
    // 找到fd对应的FdContext
    if ((int)m_fdContexts.size() <= fd)
    {
        return false;
    }
    FdContext *fdContext = m_fdContexts[fd];
    lock.unlock();
    FdContext::MutexType::Lock lock1(fdContext->mutex);
    if (!(fdContext->events))
    {
        return false;
    }

    // 删除全部事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    memset(&epevent, 0, sizeof(epoll_event));
    epevent.events = 0;
    epevent.data.ptr = fdContext;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        DBSPIDER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << op << ", " << fd << ", "
                                        << epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 触发全部已注册的事件
    if (fdContext->events & READ)
    {
        fdContext->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fdContext->events & WRITE)
    {
        fdContext->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    DBSPIDER_ASSERT(fdContext->events == 0);
    return true;
}

```

### 2.6 IOmanager的析构和停止

#### 2.6.1 IOmanager的析构

+ 对于IOManager的析构，首先要等Scheduler调度完所有的任务，然后再关闭epoll句柄和pipe句柄，然后释放所有的FdContext。

```C++
IOManager::~IOManager()
{
    sleep(3);
    m_stop = true;

    while (!stopping())
    {
        sleep(3);
    }

    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i)
    {
        if (m_fdContexts[i])
        {
            delete m_fdContexts[i];
        }
    }
}
```

#### 2.6.2 IOmanager的停止

+ IOManager在判断是否可退出时，还要加上所有IO事件都完成调度的条件。

```C++
bool IOManager::stopping()
{
    uint64_t timeout = 0;
    return stopping(timeout);
}

// 判断是否可以停止
bool IOManager::stopping(uint64_t &timeout)
{
    timeout = getNextTimer();

    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}
```

## 总结

+ IO协程调度模块可分为两部分：
  + 第一部分是对协程调度器的改造，将 `epoll` 与协程调度融合，重载 `notify` 和 `wait` ，并保证原有的功能不变。
  + 第二部分是基于 `epoll` 实现IO事件的添加、删除、调度、取消等功能。

+ IO协程调度关注的是 `FdContext` 信息，也就是描述符-事件-回调函数三元组， `IOManager` 需要保存所有关注的三元组，并且在 `epoll_wait` 检测到描述符事件就绪时执行对应的回调函数。
+ `epoll` 是**线程安全**的，即使调度器有多个调度线程，它们也可以共用同一个 `epoll` 实例，而不用担心互斥。由于空闲时所有线程都阻塞的 `epoll_wait` 上，所以也不用担心**CPU占用**问题。
+ `addEvent` 是一次性的，比如说，注册了一个读事件，当 `fd` 可读时会触发该事件，但触发完之后，这次注册的事件就失效了，后面 `fd` 再次可读时，并不会继续执行该事件回调，如果要持续触发事件的回调，那每次事件处理完都要手动再 `addEvent` 。这样在应对 `fd` 的 `WRITE` 事件时会比较好处理，因为 `fd` 可写是常态，如果注册一次就一直有效，那么可写事件就必须在执行完之后就删除掉。
+ `FdContext` 的寻址问题，框架使用 `fd` 的值作为 `FdContext` 数组的下标，这样可以快速找到一个 `fd` 对应的 `FdContext` 。由于关闭的 fd 会被重复利用，所以这里也不用担心 `FdContext` 数组膨胀太快，或是利用率低的问题。
+ IO协程调度器的退出，不但所有协程要完成调度，所有IO事件也要完成调度。
