#include "rpc_service_registry.h"

void rpc_service_registry()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8070");
    dbspider::rpc::RpcServiceRegistry::ptr server(new dbspider::rpc::RpcServiceRegistry());
    while (!server->bind(address))
    {
        sleep(1);
    }
    server->start();
}

void test_publish()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8070");
    dbspider::rpc::RpcServiceRegistry::ptr server(new dbspider::rpc::RpcServiceRegistry());

    while (!server->bind(address))
    {
        sleep(1);
    }

    server->start();

    Go
    {
        int n = 0;
        std::vector<int> vec;
        while (true)
        {
            vec.push_back(n);
            sleep(3);
            server->publish("data", vec);
            ++n;
        }
    };
}
int main()
{
    go test_publish;
}