#pragma once

#include <string>
#include <map>
#include <string>
#include <sstream>
#include <functional>
#include <memory>
#include "serializer.h"

namespace dbspider::rpc
{
    // 连接池向注册中心订阅的前缀
    inline const char *RPC_SERVICE_SUBSCRIBE = "[[rpc service subscribe]]";

    template <typename T>
    struct return_type
    {
        using type = T;
    };

    template <>
    struct return_type<void>
    {
        using type = int8_t;
    };

    // 调用结果为 void 类型的，将类型转换为 int8_t
    template <typename T>
    using return_type_t = typename return_type<T>::type;

    // RPC调用状态
    enum RpcState
    {
        RPC_SUCCESS = 0, // 成功
        RPC_FAIL,        // 失败
        RPC_NO_MATCH,    // 函数不匹配
        RPC_NO_METHOD,   // 没有找到调用函数
        RPC_CLOSED,      // RPC 连接被关闭
        RPC_TIMEOUT      // RPC 调用超时
    };

    // 包装 RPC 调用结果
    template <typename T = void>
    class Result
    {
    public:
        using row_type = T;
        using type = return_type_t<T>;
        using msg_type = std::string;
        using code_type = uint16_t;

        static Result<T> Success()
        {
            Result<T> res;
            res.setCode(RPC_SUCCESS);
            res.setMsg("success");
            return res;
        }

        static Result<T> Fail()
        {
            Result<T> res;
            res.setCode(RPC_FAIL);
            res.setMsg("fail");
            return res;
        }

        Result() {}

        bool valid() { return m_code == 0; }

        void setCode(code_type code) { m_code = code; }
        void setMsg(msg_type msg) { m_msg = msg; }
        void setVal(const type &val) { m_val = val; }

        int getCode() { return m_code; }
        const msg_type &getMsg() { return m_msg; }
        type &getVal() { return m_val; }

        type *operator->() noexcept { return &m_val; }
        const type *operator->() const noexcept { return &m_val; }

        // 调试使用 ！！！！！！
        std::string toString()
        {
            std::stringstream ss;
            ss << "[ code=" << m_code << " msg=" << m_msg << " val=" << m_val << " ]";
            return ss.str();
        }

        // 反序列化回 Result
        friend Serializer &operator>>(Serializer &in, Result<T> &d)
        {
            in >> d.m_code >> d.m_msg;
            if (d.m_code == 0)
            {
                in >> d.m_val;
            }
            return in;
        }

        // 将 Result 序列化
        friend Serializer &operator<<(Serializer &out, Result<T> d)
        {
            out << d.m_code << d.m_msg << d.m_val;
            return out;
        }

    private:
        code_type m_code = 0; // 调用状态
        msg_type m_msg;       // 调用消息
        type m_val;           // 调用结果
    };

}
