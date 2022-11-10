#include "dbspider.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include "stdio.h"

static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

// 自定义函数，用于设置套接字为非阻塞套接字
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option); //设置为非阻塞套接字
    return old_option;
}

void test_fiber()
{
    DBSPIDER_LOG_DEBUG(g_logger) << "test_fiber start";
    dbspider::Fiber::YieldToReady();
    sleep(3);
    DBSPIDER_LOG_DEBUG(g_logger) << "test_fiber end";
}

void test1()
{
    dbspider::IOManager ioManager(2);
    ioManager.submit(&test_fiber);
    ioManager.submit(std::make_shared<dbspider::Fiber>(test_fiber));
}

void test2()
{
    DBSPIDER_LOG_DEBUG(g_logger) << dbspider::Fiber::GetThis()->getState();
    dbspider::IOManager ioManager(2);
    // int sock = socket(AF_INET, SOCK_STREAM, 0);
    // setnonblocking(sock);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);
    //    ioManager.addEvent(sock,dbspider::IOManager::READ,[sock](){
    //        while(1){
    //            char buff[100];
    //            memset(buff,0,100);
    //            recv(sock,buff,100,0);
    //            //puts(buff);
    //            DBSPIDER_LOG_INFO(g_logger) << "Server: " <<buff;
    //            dbspider::Fiber::YieldToReady();
    //        }
    //    });
    dbspider::IOManager::GetThis()->addEvent(STDIN_FILENO, dbspider::IOManager::READ, []()
                                             {
        while (true){
            DBSPIDER_LOG_DEBUG(g_logger) << "connected r";
            char buff[100];
            DBSPIDER_LOG_INFO(g_logger) << "STDIN: " << fgets(buff,100,stdin);
            dbspider::Fiber::YieldToReady();
            //break;

        } });
    // dbspider::IOManager::GetThis()->cancelEvent(STDIN_FILENO,dbspider::IOManager::READ);
    // int cfd = connect(sock, (const sockaddr*)&addr, sizeof(addr));
    // DBSPIDER_LOG_INFO(g_logger)<<cfd;
    // setnonblocking(cfd);
}

void test3()
{
    DBSPIDER_LOG_DEBUG(g_logger) << dbspider::Fiber::GetThis()->getState();
    dbspider::IOManager ioManager(3);
    dbspider::IOManager::GetThis()->addEvent(STDIN_FILENO, dbspider::IOManager::READ, []()
                                             {
        while (true){
            DBSPIDER_LOG_DEBUG(g_logger) << "connected r";
            char buff[100];
            DBSPIDER_LOG_INFO(g_logger) << fgets(buff,100,stdin);
            //dbspider::Fiber::YieldToReady();
            //break;
            //dbspider::IOManager::GetThis()->addEvent(STDIN_FILENO, dbspider::IOManager::READ,)
        } });
}

void test_cb()
{
    DBSPIDER_LOG_INFO(g_logger) << "test cb";
    char buff[100];
    DBSPIDER_LOG_INFO(g_logger) << fgets(buff, 100, stdin);
    dbspider::IOManager::GetThis()->addEvent(STDIN_FILENO, dbspider::IOManager::READ, &test_cb);
}

void test4()
{
    DBSPIDER_LOG_DEBUG(g_logger) << dbspider::Fiber::GetThis()->getState();
    dbspider::IOManager ioManager(2);
    // dbspider::IOManager::GetThis()->addEvent(STDIN_FILENO, dbspider::IOManager::READ, &test_cb);
}

int main()
{
    // dbspider::Fiber::EnableFiber();
    test1();
    // std::cout<<sizeof(D);
}