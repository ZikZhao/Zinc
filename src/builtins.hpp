#include "pch.hpp"

#include "object.hpp"

class StringViewType final : public ClassType {
    friend class TypeRegistry;

private:
    StringViewType() : ClassType("string_view", nullptr, {}, get_attr(), get_methods()) {}

    GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> get_methods() {
        return GlobalMemory::Map<std::string_view, OverloadedFunctionValue*>{
            {"init",
             new OverloadedFunctionValue(new FunctionType(
                 GlobalMemory::pack_array<Type*>(
                     TypeRegistry::get<ArrayType>(&IntegerType::u8_instance)
                 ),
                 this
             ))},
            {"at",
             new OverloadedFunctionValue(new FunctionType(
                 GlobalMemory::pack_array<Type*>(&IntegerType::u64_instance), &AnyType::instance
             ))}
        };
    }

    GlobalMemory::Map<std::string_view, Type*> get_attr() {
        return GlobalMemory::Map<std::string_view, Type*>{{"length_", &IntegerType::u64_instance}};
    }
};

class StringType final : public ClassType {
    friend class TypeRegistry;

private:
    StringType() : ClassType("string", nullptr, {}, get_attr(), get_methods()) {}

    GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> get_methods() {
        return GlobalMemory::Map<std::string_view, OverloadedFunctionValue*>{
            {"at",
             new OverloadedFunctionValue(new FunctionType(
                 GlobalMemory::pack_array<Type*>(&IntegerType::u64_instance), &AnyType::instance
             ))}
        };
    }

    GlobalMemory::Map<std::string_view, Type*> get_attr() {
        return GlobalMemory::Map<std::string_view, Type*>{{"length_", &IntegerType::u64_instance}};
    }
};
