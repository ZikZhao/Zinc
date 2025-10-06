#pragma once
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <functional>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

template<typename ValueType>
using Map = std::unordered_map<std::string, ValueType>;

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
