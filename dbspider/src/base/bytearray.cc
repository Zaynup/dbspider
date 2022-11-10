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
        if (size == 0)
        {
            return;
        }
        addCapacity(size);

        size_t npos = m_position % m_baseSize; // 当前操作位置
        size_t ncap = m_cur->size - npos;      // 当前内存块容量
        size_t bpos = 0;

        while (size > 0)
        {
            if (ncap >= size)
            {
                memcpy(m_cur->ptr + npos, (const char *)buf + bpos, size);
                if (m_cur->size == (npos + size))
                {
                    m_cur = m_cur->next;
                }
                m_position += size;
                bpos += size;
                size = 0;
            }
            else
            {
                memcpy(m_cur->ptr + npos, (const char *)buf + bpos, ncap);
                m_position += ncap;
                bpos += ncap;
                size -= ncap;
                m_cur = m_cur->next;
                ncap = m_baseSize;
                npos = 0;
            }
        }

        if (m_position > m_size)
        {
            m_size = m_position;
        }
    }

    // 读取size长度的数据
    void ByteArray::read(void *buf, size_t size)
    {
        if (size > getReadSize())
        {
            throw std::out_of_range("not enough len");
        }

        size_t npos = m_position % m_baseSize;
        size_t ncap = m_cur->size - npos;
        size_t bpos = 0;

        while (size > 0)
        {
            if (ncap >= size)
            {
                memcpy((char *)buf + bpos, m_cur->ptr + npos, size);
                if (m_cur->size == (npos + size))
                {
                    m_cur = m_cur->next;
                }
                m_position += size;
                bpos += size;
                size = 0;
            }
            else
            {
                memcpy((char *)buf + bpos, m_cur->ptr + npos, ncap);
                m_position += ncap;
                bpos += ncap;
                size -= ncap;
                m_cur = m_cur->next;
                ncap = m_cur->size;
                npos = 0;
            }
        }
    }

    // 从position处开始读取size长度的数据
    void ByteArray::read(void *buf, size_t size, size_t position) const
    {
        if (size > (m_size - position))
        {
            throw std::out_of_range("not enough len");
        }

        size_t npos = position % m_baseSize;
        size_t ncap = m_cur->size - npos;
        size_t bpos = 0;
        Node *cur = m_cur;

        while (size > 0)
        {
            if (ncap >= size)
            {
                memcpy((char *)buf + bpos, cur->ptr + npos, size);
                if (cur->size == (npos + size))
                {
                    cur = cur->next;
                }
                position += size;
                bpos += size;
                size = 0;
            }
            else
            {
                memcpy((char *)buf + bpos, cur->ptr + npos, ncap);
                position += ncap;
                bpos += ncap;
                size -= ncap;
                cur = cur->next;
                ncap = cur->size;
                npos = 0;
            }
        }
    }

    // 把ByteArray的数据写入到文件中
    bool ByteArray::writeToFile(const std::string &name) const
    {
        // 打开文件
        std::ofstream ofs;
        ofs.open(name, std::ios::trunc | std::ios::binary);
        if (!ofs)
        {
            DBSPIDER_LOG_ERROR(g_logger) << "writeToFile name=" << name
                                         << " error , errno=" << errno << " errstr=" << strerror(errno);
            return false;
        }

        int64_t read_size = getReadSize();
        int64_t pos = m_position;
        Node *cur = m_cur;

        while (read_size > 0)
        {
            int diff = pos % m_baseSize;
            int64_t len = (read_size > (int64_t)m_baseSize ? m_baseSize : read_size) - diff;
            ofs.write(cur->ptr + diff, len);
            cur = cur->next;
            pos += len;
            read_size -= len;
        }

        return true;
    }

    // 从文件中读取数据
    bool ByteArray::readFromFile(const std::string &name)
    {
        std::ifstream ifs;
        ifs.open(name, std::ios::binary);
        if (!ifs)
        {
            DBSPIDER_LOG_ERROR(g_logger) << "readFromFile name=" << name
                                         << " error, errno=" << errno << " errstr=" << strerror(errno);
            return false;
        }

        std::shared_ptr<char> buff(new char[m_baseSize],
                                   [](char *ptr)
                                   {
                                       delete[] ptr;
                                   });

        while (!ifs.eof())
        {
            ifs.read(buff.get(), m_baseSize);
            write(buff.get(), ifs.gcount());
        }

        return true;
    }

    // 获取可读取的缓存,保存成iovec数组
    uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len) const
    {
        len = len > getReadSize() ? getReadSize() : len;
        if (len == 0)
        {
            return 0;
        }

        uint64_t size = len;

        size_t npos = m_position % m_baseSize;
        size_t ncap = m_cur->size - npos;
        struct iovec iov;
        Node *cur = m_cur;

        while (len > 0)
        {
            if (ncap >= len)
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = len;
                len = 0;
            }
            else
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = ncap;
                len -= ncap;
                cur = cur->next;
                ncap = cur->size;
                npos = 0;
            }
            buffers.push_back(iov);
        }
        return size;
    }

    // 获取可读取的缓存,保存成iovec数组,从position位置开始
    uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len, uint64_t position) const
    {
        len = len > getReadSize() ? getReadSize() : len;
        if (len == 0)
        {
            return 0;
        }

        uint64_t size = len;

        size_t npos = position % m_baseSize;
        size_t count = position / m_baseSize;
        Node *cur = m_root;
        while (count > 0)
        {
            cur = cur->next;
            --count;
        }

        size_t ncap = cur->size - npos;
        struct iovec iov;
        while (len > 0)
        {
            if (ncap >= len)
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = len;
                len = 0;
            }
            else
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = ncap;
                len -= ncap;
                cur = cur->next;
                ncap = cur->size;
                npos = 0;
            }
            buffers.push_back(iov);
        }
        return size;
    }

    // 获取可写入的缓存,保存成iovec数组
    uint64_t ByteArray::getWriteBuffers(std::vector<iovec> &buffers, uint64_t len)
    {
        if (len == 0)
        {
            return 0;
        }
        addCapacity(len);
        uint64_t size = len;

        size_t npos = m_position % m_baseSize;
        size_t ncap = m_cur->size - npos;
        struct iovec iov;
        Node *cur = m_cur;
        while (len > 0)
        {
            if (ncap >= len)
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = len;
                len = 0;
            }
            else
            {
                iov.iov_base = cur->ptr + npos;
                iov.iov_len = ncap;

                len -= ncap;
                cur = cur->next;
                ncap = cur->size;
                npos = 0;
            }
            buffers.push_back(iov);
        }
        return size;
    }

    // 设置ByteArray当前位置
    void ByteArray::setPosition(size_t v)
    {
        if (v > m_capacity)
        {
            throw std::out_of_range("set_position out of range");
        }

        m_position = v;

        if (m_position > m_size)
        {
            m_size = m_position;
        }

        m_cur = m_root;
        while (v > m_cur->size)
        {
            v -= m_cur->size;
            m_cur = m_cur->next;
        }

        if (v == m_cur->size)
        {
            m_cur = m_cur->next;
        }
    }

    // 是否是小端
    bool ByteArray::isLittleEndian() const
    {
        return m_endian == std::endian::little;
    }

    // 设置是否为小端
    void ByteArray::setIsLittleEndian(bool val)
    {
        if (val)
        {
            m_endian = std::endian::little;
        }
        else
        {
            m_endian = std::endian::big;
        }
    }

    // 将ByteArray里面的数据[m_position, m_size)转成std::string
    std::string ByteArray::toString() const
    {
        std::string str;
        str.resize(getReadSize());
        if (str.empty())
        {
            return str;
        }
        read(&str[0], str.size(), m_position);
        return str;
    }

    // 将ByteArray里面的数据[m_position, m_size)转成16进制的std::string(格式:FF FF FF)
    std::string ByteArray::toHexString() const
    {
        std::string str = toString();
        std::stringstream ss;

        for (size_t i = 0; i < str.size(); ++i)
        {
            if (i > 0 && i % 32 == 0)
            {
                ss << std::endl;
            }
            ss << std::setw(2)
               << std::setfill('0')
               << std::hex
               << (int)(uint8_t)str[i]
               << " ";
        }

        return ss.str();
    }

    // 扩容ByteArray,使其可以容纳size个数据(如果原本可以可以容纳,则不扩容)
    void ByteArray::addCapacity(size_t size)
    {
        if (size == 0)
        {
            return;
        }

        // 获取ByteArray原有容量
        size_t old_cap = getCapacity();

        // 如果容量够用，直接返回
        if (old_cap >= size)
        {
            return;
        }

        // 计算要增加的容量
        size = size - old_cap;

        // 要添加的内存块数量
        size_t count = ceil(1.0 * size / m_baseSize);

        // 找到最后一个内存块，即从这里开始添加内存块
        Node *tmp = m_root;
        while (tmp->next)
        {
            tmp = tmp->next;
        }

        // 记录添加后的第一个内存块
        Node *first = nullptr;

        for (size_t i = 0; i < count; ++i)
        {
            tmp->next = new Node(m_baseSize);
            if (first == nullptr)
            {
                first = tmp->next;
            }
            tmp = tmp->next;
            m_capacity += m_baseSize;
        }

        // 如果原有容量已经为0，那么当前内存块指针指向添加的第一个内存块
        if (old_cap == 0)
        {
            m_cur = first;
        }
    }
}