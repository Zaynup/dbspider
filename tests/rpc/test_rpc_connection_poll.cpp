#include "rpc_connection_pool.h"
#include "serializer.h"
#include "log.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

void test_call()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8070");
    // dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>(5);
    dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>();
    con->connect(address);

    //    auto aa = con->call<int>("add",1,1);
    //    DBSPIDER_LOG_INFO(g_logger) << aa.toString();
    //    aa = con->call<int>("add",2,2);
    //    DBSPIDER_LOG_INFO(g_logger) << aa.toString();
    //    aa = con->call<int>("add",3,3);
    //    DBSPIDER_LOG_INFO(g_logger) << aa.toString();
    // std::future<dbspider::rpc::Result<std::string>> b = con->async_call<std::string>("getStr");

    std::vector<std::string> vec{"a-", "b-", "c"};
    con->callback("CatString", vec, [](dbspider::rpc::Result<std::string> str)
                  { DBSPIDER_LOG_INFO(g_logger) << str.toString(); });
    con->callback("CatString", vec, [](dbspider::rpc::Result<std::string> str)
                  { DBSPIDER_LOG_INFO(g_logger) << str.toString(); });
    // sleep(4);
    int n = 0;
    while (n != 10000)
    {
        DBSPIDER_LOG_DEBUG(g_logger) << n++;
        con->callback("add", 0, n, [](dbspider::rpc::Result<int> res)
                      { DBSPIDER_LOG_DEBUG(g_logger) << res.toString(); });
        //        auto rt = con->call<int>("add",0,n);
        //        DBSPIDER_LOG_DEBUG(g_logger) << rt.toString();
    }
    // sleep(5);
    //    DBSPIDER_LOG_INFO(g_logger) << b.get().toString();
    //    DBSPIDER_LOG_INFO(g_logger) << a.get().toString();
}

void test_subscribe()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8070");
    // dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>(5);
    dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>();
    con->connect(address);
    con->subscribe("data",
                   [](dbspider::rpc::Serializer s)
                   {
                       std::vector<int> vec;
                       s >> vec;
                       std::string str;
                       std::for_each(vec.begin(), vec.end(), [&str](int i) mutable
                                     { str += std::to_string(i); });
                       LOG_DEBUG << "recv publish: " << str;
                   });
    while (true)
    {
        sleep(5);
    }
}

void test_subscribe_service()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8070");
    dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>();
    con->connect(address);

    auto aa = con->call<int>("add", 1, 1);
    DBSPIDER_LOG_INFO(g_logger) << aa.toString();

    while (true)
    {
        // 将 test_rpc_server 断开，会看到控制台打印 service [ add : 127.0.0.1:8080 ] quit
        // 将 test_rpc_server 重新连接，会看到控制台打印 service [ add : 127.0.0.1:8080 ] join
        // 实时的发布/订阅模式实现自动维护服务列表
        sleep(1);
    }
}

int main()
{
    // go test_subscribe;
    go test_subscribe_service;
}