#include "pch.hpp"

#include "object.hpp"

class StringViewType final : public ClassType {
    friend class TypeRegistry;

private:
    static GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> get_methods() {
        return GlobalMemory::Map<std::string_view, OverloadedFunctionValue*>{
            {"at",
             new OverloadedFunctionValue(new FunctionType(
                 GlobalMemory::pack_array<Type*>(&IntegerType::u64_instance), &AnyType::instance
             ))}
        };
    }

    static GlobalMemory::Map<std::string_view, Type*> get_attr() {
        return GlobalMemory::Map<std::string_view, Type*>{{"length_", &IntegerType::u64_instance}};
    }

private:
    StringViewType() : ClassType("string_view", {}, nullptr, get_methods(), get_attr()) {}
};

class StringType final : public ClassType {
    friend class TypeRegistry;

private:
    static GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> get_methods() {
        return GlobalMemory::Map<std::string_view, OverloadedFunctionValue*>{
            {"at",
             new OverloadedFunctionValue(new FunctionType(
                 GlobalMemory::pack_array<Type*>(&IntegerType::u64_instance), &AnyType::instance
             ))}
        };
    }

    static GlobalMemory::Map<std::string_view, Type*> get_attr() {
        return GlobalMemory::Map<std::string_view, Type*>{{"length_", &IntegerType::u64_instance}};
    }

private:
    StringType() : ClassType("string", {}, nullptr, get_methods(), get_attr()) {}
};
