#include "dbspider.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

class EchoServer : public dbspider::TcpServer
{
public:
    void handleClient(dbspider::Socket::ptr client) override
    {
        DBSPIDER_LOG_INFO(g_logger) << "handleClient" << client->toString();
        dbspider::ByteArray::ptr buff(new dbspider::ByteArray);
        while (true)
        {
            buff->clear();
            std::vector<iovec> iovs;
            buff->getWriteBuffers(iovs, 1024);

            int n = client->recv(&iovs[0], iovs.size());
            if (n == 0)
            {
                DBSPIDER_LOG_INFO(g_logger) << "Client Close" << client->toString();
                break;
            }
            else if (n < 0)
            {
                DBSPIDER_LOG_INFO(g_logger) << "Client Error, errno=" << errno << " errstr=" << strerror(errno);
                break;
            }
            buff->setPosition(buff->getPosition() + n);
            buff->setPosition(0);
            DBSPIDER_LOG_INFO(g_logger) << "Client: " << buff->toString();
        }
    }
};
void run()
{
    EchoServer::ptr echoServer(new EchoServer);
    dbspider::Address::ptr address = dbspider::Address::LookupAny("127.0.0.1:8080");

    while (!echoServer->bind(address))
    {
        sleep(2);
    }
    echoServer->start();
}
int main()
{
    go run;
}