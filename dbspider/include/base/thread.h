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

    private:
        pid_t m_id = 0;
        pthread_t m_thread = 0;
        callback m_cb;
        std::string m_name;
        Semaphore m_sem{1};
    };
}
