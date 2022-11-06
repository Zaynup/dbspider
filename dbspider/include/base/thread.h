#pragma once

#include <atomic>
#include <string>
#include <memory>
#include <functional>
#include <pthread.h>
#include <unistd.h>

#include "dbspider/include/base/sync.h"

namespace dbspider
{
    class Thread
    {
    public:
        using ptr = std::shared_ptr<Thread>;
        using callback = std::function<void()>;

        // 构造函数  线程名字和回调函数
        Thread(const std::string &name, callback cb);
        ~Thread();

        // 等待子线程返回
        void join();

        static void *run(void *arg);

        static Thread *GetThis();
        static const std::string &GetName();
        static void SetName(const std::string &name);

        const std::string &getName() const { return m_name; }
        pid_t getId() const { return m_id; }

    private:
        // 禁用拷贝赋值
        Thread(const Thread &thread) = delete;
        Thread(const Thread &&thread) = delete;
        Thread &operator=(const Thread &thread) = delete;

        pid_t m_id = 0;
        pthread_t m_thread = 0;
        callback m_cb;
        std::string m_name;
        Semaphore m_sem{1};
    };
}
