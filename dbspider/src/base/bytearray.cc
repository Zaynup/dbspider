#include <fstream>
#include <sstream>
#include <string.h>
#include <iomanip>
#include <endian.h>

#include "bytearray.h"
#include "log.h"

namespace dbspider
{
    static dbspider::Logger::ptr g_logger = DBSPIDER_LOG_NAME("system");

    ByteArray::Node::Node()
        : ptr(nullptr),
          next(nullptr),
          size(0)
    {
    }

    ByteArray::Node::Node(size_t s)
        : ptr(new char[s]),
          next(nullptr),
          size(s)
    {
    }

    ByteArray::Node::~Node()
    {
        if (ptr)
        {
            delete[] ptr;
        }
    }

    ByteArray::ByteArray(size_t basesize)
        : m_baseSize(basesize),
          m_position(0),
          m_capacity(basesize),
          m_size(0),
          m_endian(std::endian::big),
          m_root(new Node(basesize)),
          m_cur(m_root)
    {
    }

    ByteArray::~ByteArray()
    {
        Node *tmp = m_root;
        while (tmp)
        {
            m_cur = tmp;
            tmp = tmp->next;
            delete m_cur;
        }
    }

    // 清空ByteArray
    void ByteArray::clear()
    {
        m_position = m_size = 0;
        m_capacity = m_baseSize;
        Node *tmp = m_root;
        while (tmp)
        {
            m_cur = tmp;
            tmp = tmp->next;
            delete m_cur;
        }
        m_cur = m_root;
        m_root->next = nullptr;
    }

    // 写入size长度的数据
    void ByteArray::write(const void *buf, size_t size)
    {
    }

    // 读取size长度的数据
    void ByteArray::read(void *buf, size_t size)
    {
    }

    // 读取size长度的数据
    void ByteArray::read(void *buf, size_t size, size_t position) const
    {
    }

    // 把ByteArray的数据写入到文件中
    bool ByteArray::writeToFile(const std::string &name) const
    {
    }

    // 从文件中读取数据
    bool ByteArray::readFromFile(const std::string &name)
    {
    }

    // 获取可读取的缓存,保存成iovec数组
    uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len = ~0ull) const
    {
    }

    // 获取可读取的缓存,保存成iovec数组,从position位置开始
    uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len, uint64_t position) const
    {
    }

    // 获取可写入的缓存,保存成iovec数组
    uint64_t ByteArray::getWriteBuffers(std::vector<iovec> &buffers, uint64_t len)
    {
    }

    // 设置ByteArray当前位置
    void ByteArray::setPosition(size_t v)
    {
    }

    // 是否是小端
    bool ByteArray::isLittleEndian() const
    {
    }

    // 设置是否为小端
    void ByteArray::setIsLittleEndian(bool val)
    {
    }

    // 将ByteArray里面的数据[m_position, m_size)转成std::string
    std::string ByteArray::toString() const
    {
    }

    // 将ByteArray里面的数据[m_position, m_size)转成16进制的std::string(格式:FF FF FF)
    std::string ByteArray::toHexString() const
    {
    }
}