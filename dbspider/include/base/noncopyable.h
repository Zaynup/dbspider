#pragma once

class Noncopyable
{
public:
    Noncopyable() = default;
    ~Noncopyable() = default;
    Noncopyable(const Noncopyable &noncopyable) = delete;
    Noncopyable &operator=(const Noncopyable *noncopyable) = delete;
};