#include "dbspider.h"

// 服务注册中心
void Main()
{
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8080");
    dbspider::rpc::RpcServiceRegistry::ptr server = std::make_shared<dbspider::rpc::RpcServiceRegistry>();
    // 服务注册中心绑定在8080端口
    while (!server->bind(address))
    {
        sleep(1);
    }
    server->start();
}

int main()
{
    go Main;
}