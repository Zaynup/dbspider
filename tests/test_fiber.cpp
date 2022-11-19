#include "fiber.h"
#include "log.h"
#include "io_manager.h"

dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();
int count = 0;

void run_in_fiber()
{
    DBSPIDER_LOG_INFO(g_logger) << "run_in_fiber begin";
    dbspider::Fiber::YieldToReady();
    DBSPIDER_LOG_INFO(g_logger) << "run_in_fiber end";
}

void test1()
{
    DBSPIDER_LOG_INFO(g_logger) << "begin";
    dbspider::Fiber::ptr fiber(new dbspider::Fiber(run_in_fiber));
    fiber->resume();
    DBSPIDER_LOG_INFO(g_logger) << "after swap in";
    fiber->resume();
    DBSPIDER_LOG_INFO(g_logger) << "end";
}

void test2()
{
    dbspider::Fiber::ptr fiber(new dbspider::Fiber(
        []()
        {
            while (1)
            {
                count++;
                if (count == 20000)
                {
                    return;
                }
                dbspider::Fiber::YieldToReady();
            }
        }));

    while (1)
    {
        fiber->resume();
        DBSPIDER_LOG_DEBUG(g_logger) << count;
    }
}

int main(int argc, char **argv)
{
    dbspider::Fiber::EnableFiber();
    go test1;
}