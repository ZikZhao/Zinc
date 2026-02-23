#include "pch.hpp"

#include "object.hpp"

// class StringViewType final : public ClassType {
//     friend class TypeRegistry;

// private:
//     StringViewType() : ClassType("string_view", nullptr, {}, get_attr(), get_methods()) {}

//     GlobalMemory::FlatMap<std::string_view, FunctionOverloads> get_methods() {
//         return GlobalMemory::FlatMap<std::string_view, FunctionOverloads>{
//             {"init",
//              {new FunctionType(
//                  GlobalMemory::pack_array<const Type*>(
//                      TypeRegistry::get<ArrayType>(&IntegerType::u8_instance)
//                  ),
//                  this
//              )}},
//             {"at",
//              {new FunctionType(
//                  GlobalMemory::pack_array<const Type*>(&IntegerType::u64_instance),
//                  &AnyType::instance
//              )}},
//         };
//     }

//     GlobalMemory::FlatMap<std::string_view, const Type*> get_attr() {
//         return GlobalMemory::FlatMap<std::string_view, const Type*>{
//             {"length_", &IntegerType::u64_instance}
//         };
//     }
// };

// class StringType final : public ClassType {
//     friend class TypeRegistry;

// private:
//     StringType() : ClassType("string", nullptr, {}, get_attr(), get_methods()) {}

//     GlobalMemory::FlatMap<std::string_view, FunctionOverloads> get_methods() {
//         return GlobalMemory::FlatMap<std::string_view, FunctionOverloads>{
//             {"at",
//              {new FunctionType(
//                  GlobalMemory::pack_array<const Type*>(&IntegerType::u64_instance),
//                  &AnyType::instance
//              )}},
//         };
//     }

//     GlobalMemory::FlatMap<std::string_view, const Type*> get_attr() {
//         return GlobalMemory::FlatMap<std::string_view, const Type*>{
//             {"length_", &IntegerType::u64_instance}
//         };
//     }
// };
