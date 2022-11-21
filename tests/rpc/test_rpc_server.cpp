#include "rpc_server.h"

int add(int a, int b)
{
    return a + b;
}

std::string getStr()
{
    return "hello world";
}

std::string CatString(std::vector<std::string> v)
{
    std::string res;
    for (auto &s : v)
    {
        res += s;
    }
    return res;
}

int main(int argc, char **argv)
{
    int port = 8080;
    if (argv[1])
    {
        port = std::stoi(argv[1]);
    }

    dbspider::Address::ptr address = dbspider::IPv4Address::Create("127.0.0.1", port);
    // dbspider::Address::ptr registry = dbspider::Address::LookupAny("127.0.0.1:8070");
    dbspider::rpc::RpcServer::ptr server(new dbspider::rpc::RpcServer());
    std::string str = "lambda";
    server->registerMethod("add", add);
    server->registerMethod("getStr", getStr);
    server->registerMethod("CatString", CatString);
    server->registerMethod("sleep",
                           []
                           {
                               sleep(2);
                           });

    while (!server->bind(address))
    {
        sleep(1);
    }

    // server->bindRegistry(registry);

    server->start();

    Go
    {
        LOG_DEBUG << "start publish";
        while (true)
        {
            server->publish("iloveyou", "Yes, i love you too");
            sleep(1);
        }
    };
}