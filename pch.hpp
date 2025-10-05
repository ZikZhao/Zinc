#pragma once
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

template<uint64_t length>
class FixedString {
public:
    char str[length];
    constexpr FixedString(const char (&str)[length]) {
        std::copy_n(str, length, this->str);
    }
    constexpr std::string_view operator * () const {
        return std::string_view(str, length);
    }
};
