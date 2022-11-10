#include "dbspider.h"
#include <memory>
#include <cstdlib>

int n = 0;
dbspider::RWMutex rwmutex;
dbspider::Mutex mutex;

void f1()
{
    DBSPIDER_LOG_WARN(DBSPIDER_LOG_NAME("system"))
        << "dbspider::Thread::GetName()  " << dbspider::Thread::GetName() << std::endl
        << "dbspider::Thread::GetThis()->getName()  " << dbspider::Thread::GetThis()->getName() << std::endl
        << "dbspider::Thread::GetThis()->getId(); " << dbspider::Thread::GetThis()->getId() << std::endl
        << "dbspider::GetThreadId() " << dbspider::GetThreadId();
    // dbspider::ScopedLock<dbspider::Mutex> a(mutex);
    // dbspider::RWMutex::ReadLock a(rwmutex);
    // dbspider::RWMutex::WriteLock a(rwmutex);
    for (int i = 0; i < 10000000; i++)
    {
        // dbspider::ScopedLock<dbspider::Mutex> a(mutex);
        // dbspider::RWMutex::ReadLock a(rwmutex);
        // dbspider::RWMutex::WriteLock a(rwmutex);
        n++;
    }
}

void f2()
{
    // dbspider::Thread t;
    dbspider::TimeMeasure time;
    dbspider::Thread::ptr thread[10];

    for (int i = 0; i < 10; i++)
    {
        thread[i] = std::make_shared<dbspider::Thread>(std::to_string(i) + " t", &f1);
    }

    for (int i = 0; i < 10; i++)
    {
        thread[i]->join();
    }

    std::cout << n;
}

void p1()
{
    for (int i = 0; i < 10; ++i)
    {
        DBSPIDER_LOG_WARN(DBSPIDER_LOG_ROOT()) << "== p1 ==";
    }
};

void p2()
{
    for (int i = 0; i < 10; ++i)
    {
        DBSPIDER_LOG_ERROR(DBSPIDER_LOG_ROOT()) << "== p2 ==";
    }
};

int main()
{
    dbspider::Thread a("f1", &p1);
    dbspider::Thread b("f2", &p2);
    // dbspider::Thread c("f3", &f1);
    dbspider::Thread c("f4", &f2);

    a.join();
    b.join();
    c.join();

    DBSPIDER_LOG_INFO(DBSPIDER_LOG_ROOT()) << "finish!";

    return 0;
}