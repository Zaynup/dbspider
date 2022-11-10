#include <unistd.h>
#include "dbspider.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

void test_fiber()
{
    static int s_count = 5;
    DBSPIDER_LOG_INFO(g_logger) << "test in fiber s_count=" << s_count;

    sleep(1);
    while (--s_count >= 0)
    {
        // dbspider::Fiber::GetThis()->YieldToReady();

        // dbspider::Fiber::GetThis()->YieldToReady();
        // dbspider::Scheduler::GetThis()->submit(&test_fiber, dbspider::GetThreadId());
        dbspider::Fiber::YieldToReady();
        DBSPIDER_LOG_INFO(g_logger) << "resum  " << s_count;
    }
    DBSPIDER_LOG_INFO(g_logger) << "test end" << s_count;
}

void test_fiber2()
{
    while (1)
    {
        DBSPIDER_LOG_INFO(g_logger) << "while ";
        sleep(2);
        dbspider::Fiber::YieldToReady();
    }
}

void test3()
{
    DBSPIDER_LOG_INFO(g_logger) << "main";
    dbspider::Scheduler sc(3, "test");
    sc.start();
    sleep(2);
    DBSPIDER_LOG_INFO(g_logger) << "schedule";
    sc.submit(&test_fiber);
    // sc.submit(dbspider::Fiber::ptr (new dbspider::Fiber(&test_fiber)));
    // sc.submit(&test_fiber2);
    // sleep(8);
    // sc.stop();
    while (1)
        ;
    DBSPIDER_LOG_INFO(g_logger) << "over";
}

int main(int argc, char **argv)
{
    DBSPIDER_LOG_DEBUG(g_logger) << "main";
    dbspider::Scheduler sc(3, "test");
    sc.start();
    sc.submit(
        []
        {
            DBSPIDER_LOG_INFO(g_logger) << "hello world";
        });

    return 0;
}