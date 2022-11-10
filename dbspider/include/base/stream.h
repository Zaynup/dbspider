#pragma once

#include <memory>

#include "bytearray.h"

namespace dbspider
{
    class Stream
    {
        using ptr = std::shared_ptr<Stream>;

        virtual ~Stream(){};

        virtual ssize_t read(void *buffer, size_t length) = 0;
        virtual ssize_t read(ByteArray::ptr buffer, size_t length) = 0;

        virtual ssize_t write(const void *buffer, size_t length) = 0;
        virtual ssize_t write(ByteArray::ptr buffer, size_t length) = 0;

        virtual void close() = 0;

        // 读固定大小的数据
        ssize_t readFixSize(void *buffer, size_t length);
        ssize_t readFixSize(ByteArray::ptr buffer, size_t length);

        // 写固定大小的数据
        ssize_t writeFixSize(const void *buffer, size_t length);
        ssize_t writeFixSize(ByteArray::ptr buffer, size_t length);
    };

}