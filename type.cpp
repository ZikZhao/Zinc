#include "pch.hpp"
#include "type.hpp"

bool NullType::contains(const Type& other) const {
    return dynamic_cast<const NullType*>(&other) != nullptr;
}
NullType::operator std::string () const {
    return "null";
}

bool IntegerType::contains(const Type& other) const {
    return dynamic_cast<const IntegerType*>(&other) != nullptr;
}
IntegerType::operator std::string () const {
    return "integer";
}

bool FloatType::contains(const Type& other) const {
    return dynamic_cast<const FloatType*>(&other) != nullptr;
}
FloatType::operator std::string () const {
    return "float";
}

bool StringType::contains(const Type& other) const {
    return dynamic_cast<const StringType*>(&other) != nullptr;
}
StringType::operator std::string () const {
    return "string";
}

bool BooleanType::contains(const Type& other) const {
    return dynamic_cast<const BooleanType*>(&other) != nullptr;
}
BooleanType::operator std::string () const {
    return "boolean";
}

bool FunctionType::contains(const Type& other) const {
    if (auto func_type = dynamic_cast<const FunctionType*>(&other)) {
        if (parameter_types.size() != func_type->parameter_types.size()) {
            return false;
        }
        for (size_t i = 0; i < parameter_types.size(); i++) {
            if (not parameter_types[i].contains(*func_type->parameter_types[i])) {
                return false;
            }
        }
        return return_type.contains(*func_type->return_type);
    } else {
        return false;
    }
}
FunctionType::FunctionType(std::vector<TypeRef>&& parameter_types, TypeRef return_type)
    : parameter_types(std::forward<std::vector<TypeRef>>(parameter_types)), return_type(std::forward<TypeRef>(return_type)) {}
FunctionType::operator std::string () const {
    std::string result = "function(";
    for (size_t i = 0; i < parameter_types.size(); i++) {
        result += static_cast<std::string>(parameter_types[i]);
        if (i < parameter_types.size() - 1) {
            result += ", ";
        }
    }
    result += ") -> " + static_cast<std::string>(return_type);
    return result;
}

bool ObjectType::contains(const Type& other) const {
    if (auto obj_type = dynamic_cast<const ObjectType*>(&other)) {
        if (not extends.contains(*obj_type->extends)) {
            return false;
        }
        for (const auto& interface : interfaces) {
            bool found = false;
            for (const auto& other_interface : obj_type->interfaces) {
                if (interface.contains(*other_interface)) {
                    found = true;
                    break;
                }
            }
            if (not found) {
                return false;
            }
        }
        for (const auto& [key, type] : property_types) {
            if (obj_type->property_types.find(key) == obj_type->property_types.end()) {
                return false;
            }
            if (not type.contains(*obj_type->property_types.at(key))) {
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}
ObjectType::ObjectType(std::string_view name, std::vector<TypeRef>&& interfaces, TypeRef extends, Map<TypeRef>&& property_types)
    : name(name), interfaces(std::forward<std::vector<TypeRef>>(interfaces)), extends(std::forward<TypeRef>(extends)), property_types(std::forward<Map<TypeRef>>(property_types)) {}
ObjectType::operator std::string () const {
    std::string result = "object " + name + " implements ";
    for (size_t i = 0; i < interfaces.size(); i++) {
        result += static_cast<std::string>(interfaces[i]);
        if (i < interfaces.size() - 1) {
            result += ", ";
        }
    }
    result += " extends " + static_cast<std::string>(extends) + " {";
    size_t count = 0;
    for (const auto& [key, type] : property_types) {
        result += "\n  " + key + ": " + static_cast<std::string>(type);
        if (count < property_types.size() - 1) {
            result += ",";
        }
        count++;
    }
    result += "\n}";
    return result;
}

ListType::ListType(TypeRef element_type) : element_type(std::forward<TypeRef>(element_type)) {}
bool ListType::contains(const Type& other) const {
    if (auto list_type = dynamic_cast<const ListType*>(&other)) {
        return element_type.contains(*list_type->element_type);
    } else {
        return false;
    }
}
ListType::operator std::string () const {
    return "list<" + static_cast<std::string>(element_type) + ">";
}

DictType::DictType(TypeRef key_type, TypeRef value_type)
    : key_type(std::forward<TypeRef>(key_type)), value_type(std::forward<TypeRef>(value_type)) {}
bool DictType::contains(const Type& other) const {
    if (auto dict_type = dynamic_cast<const DictType*>(&other)) {
        return key_type.contains(*dict_type->key_type) and value_type.contains(*dict_type->value_type);
    } else {
        return false;
    }
}
DictType::operator std::string () const {
    return "dict<" + static_cast<std::string>(key_type) + ", " + static_cast<std::string>(value_type) + ">";
}

SetType::SetType(TypeRef element_type) : element_type(std::forward<TypeRef>(element_type)) {}
bool SetType::contains(const Type& other) const {
    if (auto set_type = dynamic_cast<const SetType*>(&other)) {
        return element_type.contains(*set_type->element_type);
    } else {
        return false;
    }
}
SetType::operator std::string () const {
    return "set<" + static_cast<std::string>(element_type) + ">";
}
