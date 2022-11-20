# TcpServer模块

## 1. TcpServer模块概述

+ `TcpServer` 类支持同时绑定单个或多个地址进行监听；
+ `TcpServer` 可以分别指定接收客户端和处理客户端的协程调度器。
+ `TcpServer` 类采用 **Template Pattern** 设计模式，它的 `HandleClient` 是由继承类来实现的。使用 `TcpServer` 时，必须从 `TcpServer` 派生一个新类，并重新实现子类的 `handleClient` 操作。

+ TcpServer代码框架如下：

    ```C++
    // enable_shared_from_this，本对象只能在堆上创建
    class TcpServer : public std::enable_shared_from_this<TcpServer>, Noncopyable
    {
    public:
        using ptr = std::shared_ptr<TcpServer>;

        TcpServer(IOManager *worker = IOManager::GetThis(), IOManager *accept_worker = IOManager::GetThis());
        virtual ~TcpServer();

        virtual bool bind(Address::ptr addr);
        virtual bool bind(const std::vector<Address::ptr> &addrs, std::vector<Address::ptr> &fail);

        virtual bool start();
        virtual void stop();

        .............................................................
        .............................................................

    protected:
        virtual void startAccept(Socket::ptr sock);
        virtual void handleClient(Socket::ptr client);

    protected:
        std::vector<Socket::ptr> m_listens; // 监听socket队列
        IOManager *m_worker;
        IOManager *m_acceptWorker;
        uint64_t m_recvTimeout;
        std::string m_name;
        bool m_isStop;
    };
    ```

## 2. TcpServer模块设计

### 2.1 重要成员变量

+ **m_listens** ：监听的 `socket` 队列。
+ **m_worker** ：工作协程调度器，用于处理客户端事件，对应 `handleClient` 方法。
+ **m_acceptWorker** ：接收器，用于处理客户端的连接，对应 `startAccept` 方法。

### 2.2 重要成员方法

+ `virtual bool bind(Address::ptr addr)` ：绑定单个地址
+ `virtual bool bind(const std::vector<Address::ptr> &addrs, std::vector<Address::ptr> &fail)`：绑定多个地址
+ `virtual bool start()` ：服务器开始运行，处理监听 `socket` 队列的每个 `socket` 事件。
  + 其中 `startAccept` 方法由 `m_acceptWorker` 提交调度任务，处理每个监听 `socket` 的连接事件，并让 `m_worker` 处理 `handleClient` 事件。
  + 其中 `handleClient` 方法由继承类重载，实现具体的应用。

    ```C++
    bool TcpServer::start()
    {
        if (!isStop())
        {
            return false;
        }
        m_isStop = false;
        TcpServer::ptr self = shared_from_this();
        for (auto &sock : m_listens)
        {
            m_acceptWorker->submit(
                [self, sock]
                {
                    DBSPIDER_LOG_DEBUG(g_logger) << "acceptWorker->submit";
                    self->startAccept(sock);
                });
        }
        DBSPIDER_LOG_DEBUG(g_logger) << "TcpServer::start()";

        return true;
    }  

    void TcpServer::startAccept(Socket::ptr sock)
    {
        TcpServer::ptr self = shared_from_this();
        while (!isStop())
        {
            Socket::ptr client = sock->accept();
            if (client)
            {
                client->setRecvTimeout(m_recvTimeout);
                m_worker->submit(
                    [self, client]
                    {
                        self->handleClient(client);
                    });
            }
            else
            {
                DBSPIDER_LOG_ERROR(g_logger) << "accept fail, errno=" << errno << " errstr=" << strerror(errno);
            }
        }
    }
    
    void TcpServer::handleClient(Socket::ptr client)
    {
        DBSPIDER_LOG_INFO(g_logger) << "handleClient: " << client->toString();
    }
    ```

+ `void TcpServer::stop()` : 服务器停止，取消 `socket` 的事件并关闭 `socket` 。

    ```C++
    void TcpServer::stop()
    {
        if (isStop())
        {
            return;
        }
        m_isStop = true;

        TcpServer::ptr self = shared_from_this();

        m_acceptWorker->submit(
            [self, this]
            {
                for (auto &sock : m_listens)
                {
                    sock->cancelAll();
                    sock->close();
                }
            });
    }
    ```
