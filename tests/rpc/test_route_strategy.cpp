#include "route_strategy.h"
#include "log.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();
std::vector<int> list{1, 2, 3, 4, 5};

void test_random()
{
    dbspider::rpc::RouteStrategy<int>::ptr strategy =
        dbspider::rpc::RouteEngine<int>::queryStrategy(dbspider::rpc::Strategy::Random);

    DBSPIDER_LOG_DEBUG(g_logger) << "random";
    for ([[maybe_unused]] auto i : list)
    {
        auto a = strategy->select(list);
        DBSPIDER_LOG_DEBUG(g_logger) << a;
    }
}

void test_poll()
{
    dbspider::rpc::RouteStrategy<int>::ptr strategy =
        dbspider::rpc::RouteEngine<int>::queryStrategy(dbspider::rpc::Strategy::Polling);

    DBSPIDER_LOG_DEBUG(g_logger) << "Poll";
    for ([[maybe_unused]] auto i : list)
    {
        auto a = strategy->select(list);
        DBSPIDER_LOG_DEBUG(g_logger) << a;
    }
}
void test_hash()
{
    dbspider::rpc::RouteStrategy<int>::ptr strategy =
        dbspider::rpc::RouteEngine<int>::queryStrategy(dbspider::rpc::Strategy::HashIP);

    DBSPIDER_LOG_DEBUG(g_logger) << "Hash";
    for ([[maybe_unused]] auto i : list)
    {
        auto a = strategy->select(list);
        DBSPIDER_LOG_DEBUG(g_logger) << a;
    }
}
int main()
{
    test_random();
    test_poll();
    test_hash();
}