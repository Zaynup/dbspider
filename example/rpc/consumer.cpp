#include "dbspider.h"

#include <vector>

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

// 连接服务中心，自动服务发现，执行负载均衡决策，同时会缓存发现的结果
void Main()
{
    dbspider::Address::ptr registry = dbspider::Address::LookupAny("127.0.0.1:8080");

    // 设置连接池的数量
    dbspider::rpc::RpcConnectionPool::ptr con = std::make_shared<dbspider::rpc::RpcConnectionPool>();

    // 连接服务中心
    con->connect(registry);

    dbspider::rpc::Result<int> res;

    // 第一种调用接口，以同步的方式异步调用，原理是阻塞读时会在协程池里调度
    res = con->call<int>("add", 123, 321);
    DBSPIDER_LOG_INFO(g_logger) << res.getVal();

    dbspider::rpc::Result<std::string> res1;
    res1 = con->call<std::string>("echo", "hello world");
    DBSPIDER_LOG_INFO(g_logger) << res1.getVal();

    std::vector<std::string> vec;
    vec.push_back("hello");
    vec.push_back("world");
    vec.push_back("result");
    vec.push_back("correct");

    dbspider::rpc::Result<std::vector<std::string>> res2;
    res2 = con->call<std::vector<std::string>>("revers", vec);
    std::vector<std::string> vec1(res2.getVal());
    for (int i = 0; i < 4; ++i)
    {
        DBSPIDER_LOG_INFO(g_logger) << vec1[i] << " ";
    }

    // 第二种调用接口，调用时会立即返回一个channel
    auto chan = con->async_call<int>("add", 123, 321);
    chan >> res;
    DBSPIDER_LOG_INFO(g_logger) << res.getVal();

    // 第三种调用接口，异步回调
    con->callback("add", 114514, 114514,
                  [](dbspider::rpc::Result<int> res)
                  {
                      DBSPIDER_LOG_INFO(g_logger) << res.getVal();
                  });

    // 测试并发
    int n = 0;
    while (n != 10000)
    {
        n++;
        con->callback("add", 0, n, [](dbspider::rpc::Result<int> res)
                      { DBSPIDER_LOG_INFO(g_logger) << res.getVal(); });
    }

    // 异步接口必须保证在得到结果之前程序不能退出
    sleep(3);
}

int main()
{
    go Main;
}