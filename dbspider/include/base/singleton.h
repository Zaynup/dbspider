#pragma once

#include <memory>
#include "thread.h"
namespace dbspider
{
    template <typename T>
    class Singleton
    {
    public:
        static T *GetInstance()
        {
            static T instance;
            return &instance;
        }
    };

    template <typename T>
    class SingletonPtr
    {
    public:
        static std::shared_ptr<T> GetInstance()
        {
            static std::shared_ptr<T> instance(std::make_shared<T>());
            return instance;
        }
    };

}
