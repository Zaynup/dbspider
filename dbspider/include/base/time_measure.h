#pragma once

#include <chrono>
#include <string>
#include <iostream>

namespace dbspider
{
    using namespace std;
    using namespace std::chrono;

    // 工具类，测试程序执行时间
    class TimeMeasure
    {
    public:
        TimeMeasure()
            : m_begin(high_resolution_clock::now())
        {
        }

        ~TimeMeasure()
        {
            std::cout << std::endl
                      << " cost: " << sp(elapsed()) << " 毫秒"
                      << " micro: " << sp(elapsed_micro()) << " 微妙"
                      << " nano: " << sp(elapsed_nano()) << " 纳秒" << std::endl;
        }
        void reset() { m_begin = high_resolution_clock::now(); }

        int64_t elapsed() const
        {
            return duration_cast<chrono::milliseconds>(high_resolution_clock::now() - m_begin).count();
        }

        int64_t elapsed_micro() const
        {
            return duration_cast<chrono::microseconds>(high_resolution_clock::now() - m_begin).count();
        }

        int64_t elapsed_nano() const
        {
            return duration_cast<chrono::nanoseconds>(high_resolution_clock::now() - m_begin).count();
        }

        int64_t elapsed_seconds() const
        {
            return duration_cast<chrono::seconds>(high_resolution_clock::now() - m_begin).count();
        }

        int64_t elapsed_minutes() const
        {
            return duration_cast<chrono::minutes>(high_resolution_clock::now() - m_begin).count();
        }

        int64_t elapsed_hours() const
        {
            return duration_cast<chrono::hours>(high_resolution_clock::now() - m_begin).count();
        }

    private:
        std::string sp(int64_t n)
        {
            std::string str = std::to_string(n);
            int cnt = 0;
            for (int i = str.size() - 1; i >= 0; i--)
            {
                cnt++;
                if (cnt == 3 && i)
                {
                    cnt = 0;
                    str.insert(str.begin() + i, ',');
                }
            }
            return str;
        }

    private:
        time_point<high_resolution_clock> m_begin;
    };
}
