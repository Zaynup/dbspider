#include "rpc_client.h"
#include "io_manager.h"
#include "log.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

void test1()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8080");
    dbspider::rpc::RpcClient::ptr client(new dbspider::rpc::RpcClient());

    if (!client->connect(address))
    {
        DBSPIDER_LOG_DEBUG(g_logger) << address->toString();
        return;
    }
    int n = 0;
    // std::vector<std::future<dbspider::rpc::Result<int>>> vec;
    while (n != 1000)
    {
        // DBSPIDER_LOG_DEBUG(g_logger) << n++;
        n++;
        client->callback("sleep",
                         [](dbspider::rpc::Result<> res)
                         {
                             DBSPIDER_LOG_DEBUG(g_logger) << res.toString();
                         });
    }
    auto rt = client->call<int>("add", 0, n);
    DBSPIDER_LOG_DEBUG(g_logger) << rt.toString();
    // sleep(3);
    // client->close();
    client->setTimeout(1000);
    auto sl = client->call<void>("sleep");
    DBSPIDER_LOG_DEBUG(g_logger) << "sleep 2s " << sl.toString();
}

// 测试重连
void test_retry()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8080");
    dbspider::rpc::RpcClient::ptr client(new dbspider::rpc::RpcClient());

    if (!client->connect(address))
    {
        DBSPIDER_LOG_DEBUG(g_logger) << address->toString();
        return;
    }
    client->close();
    // client = std::make_shared<dbspider::rpc::RpcClient>();

    if (!client->connect(address))
    {
        DBSPIDER_LOG_DEBUG(g_logger) << address->toString();
        return;
    }
    int n = 0;
    // std::vector<std::future<dbspider::rpc::Result<int>>> vec;
    while (n != 1000)
    {
        sleep(2);
        // DBSPIDER_LOG_DEBUG(g_logger) << n++;
        n++;
        auto res = client->call<int>("add", 1, n);
        if (res.getCode() == dbspider::rpc::RpcState::RPC_CLOSED)
        {
            // client = std::make_shared<dbspider::rpc::RpcClient>();
            if (!client->connect(address))
            {
                DBSPIDER_LOG_DEBUG(g_logger) << address->toString();
            }
            res = client->call<int>("add", 1, n);
        }
        LOG_DEBUG << res.toString();
    }
}

void subscribe()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8080");
    dbspider::rpc::RpcClient::ptr client(new dbspider::rpc::RpcClient());

    if (!client->connect(address))
    {
        DBSPIDER_LOG_DEBUG(g_logger) << address->toString();
        return;
    }

    client->subscribe("iloveyou",
                      [](dbspider::rpc::Serializer s)
                      {
                          std::string str;
                          s >> str;
                          LOG_DEBUG << str;
                      });
    while (true)
    {
        sleep(100);
    }
}

int main()
{
    // go test1;
    go subscribe;
    // go test_retry;
}
