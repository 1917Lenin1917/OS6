#pragma once

#include <iostream>
#include <sstream>
#include <new>

#define KB(x)   ((size_t) (x) << 10)
#define MB(x)   ((size_t) (x) << 20)

template <class T>
std::string to_string(T t, std::ios_base& (*f)(std::ios_base&))
{
    std::ostringstream oss;
    oss << f << static_cast<void*>(t);
    return oss.str();
}


inline std::ostream& operator<<(std::ostream& out, const std::vector<int>& v)
{
    for (const auto& value : v)
    {
        out << value << " ";
    }
    return out;
}