#include "pch.hpp"

#include "object.hpp"

class StringViewType final : public ClassType {
public:
    static StringViewType* instance;

private:
    static GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> get_methods() {
        GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> methods;
        methods.insert({"at", new OverloadedFunctionValue()}) return methods;
    }

    static GlobalMemory::Map<std::string_view, Type*> get_attr() {
        GlobalMemory::Map<std::string_view, Type*> attr;
        return attr;
    }

private:
    StringViewType() : ClassType("string_view", {}, nullptr, get_methods(), get_attr()) {}
};

class String final : public ClassType {
public:
};
