#pragma once

#include <memory>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vector>
#include "util.h"

namespace dbspider
{
    class ByteArray
    {

    public:
        struct Node
        {
            Node();

            // ByteArray的存储节点
            Node(size_t s);

            // 析构函数,释放内存
            ~Node();

            char *ptr;   // 内存块地址指针
            Node *next;  // 下一个内存块地址
            size_t size; // 内存块大小
        };

    public:
        using ptr = std::shared_ptr<ByteArray>;

        ByteArray(size_t base_size = 4096);

        ~ByteArray();

        // 写入固定长度 T 类型的数据
        template <typename T>
        void writeFint(T &value)
        {
            if (m_endian != std::endian::native)
            {
                value = ByteSwap(value);
            }
            write(&value, sizeof(value));
        }

        // 清空ByteArray
        void clear();

        // 写入size长度的数据
        void write(const void *buf, size_t size);

        // 读取size长度的数据
        void read(void *buf, size_t size);

        // 读取size长度的数据
        void read(void *buf, size_t size, size_t position) const;

        // 把ByteArray的数据写入到文件中
        bool writeToFile(const std::string &name) const;

        // 从文件中读取数据
        bool readFromFile(const std::string &name);

        // 获取可读取的缓存,保存成iovec数组
        uint64_t getReadBuffers(std::vector<iovec> &buffers, uint64_t len = ~0ull) const;

        // 获取可读取的缓存,保存成iovec数组,从position位置开始
        uint64_t getReadBuffers(std::vector<iovec> &buffers, uint64_t len, uint64_t position) const;

        // 获取可写入的缓存,保存成iovec数组
        uint64_t getWriteBuffers(std::vector<iovec> &buffers, uint64_t len);

        // 返回数据的长度
        size_t getSize() const { return m_size; }

        // 返回ByteArray当前位置
        size_t getPosition() const { return m_position; }

        // 设置ByteArray当前位置
        void setPosition(size_t v);

        // 返回内存块的大小
        size_t getBaseSize() const { return m_baseSize; }

        // 返回可读取数据大小
        size_t getReadSize() const { return m_size - m_position; }

        // 是否是小端
        bool isLittleEndian() const;

        // 设置是否为小端
        void setIsLittleEndian(bool val);

        // 将ByteArray里面的数据[m_position, m_size)转成std::string
        std::string toString() const;

        // 将ByteArray里面的数据[m_position, m_size)转成16进制的std::string(格式:FF FF FF)
        std::string toHexString() const;

        // 写入固定长度int8_t类型的数据
        void writeFint8(int8_t value);

        // 写入固定长度uint8_t类型的数据
        void writeFuint8(uint8_t value);

        // 写入固定长度int16_t类型的数据(大端/小端)
        void writeFint16(int16_t value);

        // 写入固定长度uint16_t类型的数据(大端/小端)
        void writeFuint16(uint16_t value);

        // 写入固定长度int32_t类型的数据(大端/小端)
        void writeFint32(int32_t value);

        // 写入固定长度uint32_t类型的数据(大端/小端)
        void writeFuint32(uint32_t value);

        // 写入固定长度int64_t类型的数据(大端/小端)
        void writeFint64(int64_t value);

        // 写入固定长度uint64_t类型的数据(大端/小端)
        void writeFuint64(uint64_t value);

        // 写入有符号Varint32类型的数据
        void writeInt32(int32_t value);

        // 写入无符号Varint32类型的数据
        void writeUint32(uint32_t value);

        // 写入有符号Varint64类型的数据
        void writeInt64(int64_t value);

        // 写入无符号Varint64类型的数据
        void writeUint64(uint64_t value);

        // 写入float类型的数据
        void writeFloat(float value);

        // 写入double类型的数据
        void writeDouble(double value);

        // 写入std::string类型的数据,用uint16_t作为长度类型
        void writeStringF16(const std::string &value);

        // 写入std::string类型的数据,用uint32_t作为长度类型
        void writeStringF32(const std::string &value);

        // 写入std::string类型的数据,用uint64_t作为长度类型
        void writeStringF64(const std::string &value);

        // 写入std::string类型的数据,用无符号Varint64作为长度类型
        void writeStringVint(const std::string &value);

        // 写入std::string类型的数据,无长度
        void writeStringWithoutLength(const std::string &value);

        // 读取int8_t类型的数据
        int8_t readFint8();

        // 读取uint8_t类型的数据
        uint8_t readFuint8();

        // 读取int16_t类型的数据
        int16_t readFint16();

        // 读取uint16_t类型的数据
        uint16_t readFuint16();

        // 读取int32_t类型的数据
        int32_t readFint32();

        // 读取uint32_t类型的数据
        uint32_t readFuint32();

        // 读取int64_t类型的数据
        int64_t readFint64();

        // 读取uint64_t类型的数据
        uint64_t readFuint64();

        // 读取有符号Varint32类型的数据
        int32_t readInt32();

        // 读取无符号Varint32类型的数据
        uint32_t readUint32();

        // 读取有符号Varint64类型的数据
        int64_t readInt64();

        // 读取无符号Varint64类型的数据
        uint64_t readUint64();

        // 读取float类型的数据
        float readFloat();

        // 读取double类型的数据
        double readDouble();

        // 读取std::string类型的数据,用uint16_t作为长度
        std::string readStringF16();

        // 读取std::string类型的数据,用uint32_t作为长度
        std::string readStringF32();

        // 读取std::string类型的数据,用uint64_t作为长度
        std::string readStringF64();

        // 读取std::string类型的数据,用无符号Varint64作为长度
        std::string readStringVint();

    private:
        // 扩容ByteArray,使其可以容纳size个数据(如果原本可以可以容纳,则不扩容)
        void addCapacity(size_t size);

        // 获取当前的可写入容量
        size_t getCapacity() const { return m_capacity - m_position; }

    private:
        size_t m_baseSize; // 内存块的大小
        size_t m_position; // 当前操作位置
        size_t m_capacity; // 当前的总容量
        size_t m_size;     // 当前数据的大小

        std::endian m_endian; // 字节序，默认大端
        Node *m_root;         // 第一个内存快指针
        Node *m_cur;          // 当前操作的内存块指针
    };
}
