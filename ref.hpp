#pragma once
#include <stdexcept>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include "value.hpp"
#include "type.hpp"

template<typename TargetType> class Reference;
class Variable;
using ValueRef = Reference<Value>;
using TypeRef = Reference<Type>;

namespace Constants {
    extern const ValueRef Null;
    extern const ValueRef True;
    extern const ValueRef False;
}

template<typename TargetType>
class Reference {
    friend class Variable;
private:
    std::shared_ptr<TargetType> ptr;
    Reference* source;
public:
    Reference() = default;
    Reference(const Reference& other) = default;
    Reference(Reference&& other) = default;
    Reference(TargetType* ptr) : ptr(ptr), source(nullptr) {
        use_const();
    }
    Reference(std::shared_ptr<TargetType>&& other) : ptr(std::forward<std::shared_ptr<TargetType>>(other)), source(nullptr) {
        use_const();
    }
    // Copy constructor that keeps track of source for assignment propagation
    Reference(Reference& other) : ptr(other.ptr), source(&other) {}
    template<typename U = TargetType, typename = std::enable_if_t<std::is_same_v<Value, U>>>
    Reference(LiteralType type, std::string_view literal) : ptr(Value::FromLiteral(type, literal)), source(nullptr) {
        use_const();
    }
private:
    void use_const() {
        if constexpr (std::is_same_v<TargetType, Value>) {
            if (auto ptr = dynamic_cast<NullValue*>(this->ptr.get())) {
                this->ptr = Constants::Null.ptr;
            } else if (auto ptr = dynamic_cast<BooleanValue*>(this->ptr.get())) {
                this->ptr = ptr->value ? Constants::True.ptr : Constants::False.ptr;
            }
        }
    }
public:
    // Assignment operator with adaptation
    Reference& operator = (const Reference& other) {
        if (Value* adapted = this->ptr->adapt_for_assignment(*other.ptr)) {
            this->ptr = std::shared_ptr<Value>(adapted);
        } else {
            this->ptr = other.ptr;
        }
        this->use_const();
        if (this->source) {
            *(this->source) = *this;
            this->source = nullptr;
        }
        return *this;
    }
    TargetType& operator * () const noexcept {
        return *ptr;
    }
    operator std::string () const {
        return static_cast<std::string>(this->operator*());
    }
    Reference operator + (const Reference& other) const {
        return this->operator*() + *other;
    }
    Reference operator - (const Reference& other) const {
        return this->operator*() - *other;
    }
    Reference operator - () const {
        return -this->operator*();
    }
    Reference operator * (const Reference& other) const {
        return this->operator*() * *other;
    }
    Reference operator / (const Reference& other) const {
        return this->operator*() / *other;
    }
    Reference operator % (const Reference& other) const {
        return this->operator*() % *other;
    }
    Reference operator == (const Reference& other) const {
        return this->operator*() == *other;
    }
    Reference operator != (const Reference& other) const {
        return this->operator*() != *other;
    }
    Reference operator < (const Reference& other) const {
        return this->operator*() < *other;
    }
    Reference operator <= (const Reference& other) const {
        return this->operator*() <= *other;
    }
    Reference operator > (const Reference& other) const {
        return this->operator*() > *other;
    }
    Reference operator >= (const Reference& other) const {
        return this->operator*() >= *other;
    }
    Reference operator and (const Reference& other) const {
        return this->operator*() and *other;
    }
    Reference operator or (const Reference& other) const {
        return this->operator*() or *other;
    }
    Reference operator not () const {
        return not this->operator*();
    }
    Reference operator & (const Reference& other) const {
        return this->operator*() & *other;
    }
    Reference operator | (const Reference& other) const {
        return this->operator*() | *other;
    }
    Reference operator ^ (const Reference& other) const {
        return this->operator*() ^ *other;
    }
    Reference operator ~ () const {
        return ~this->operator*();
    }
    Reference operator << (const Reference& other) const {
        return this->operator*() << *other;
    }
    Reference operator >> (const Reference& other) const {
        return this->operator*() >> *other;
    }
    Reference operator () (std::vector<Reference> args) const {
        std::vector<const TargetType*> val_args;
        for (const auto& arg : args) {
            val_args.push_back(&*arg);
        }
        return this->operator*()(val_args);
    }
    Reference operator [] (size_t index) const {
        return this->operator*()[index];
    }
    bool is_truthy() const {
        return this->operator*().is_truthy();
    }
};

using Context = std::map<std::string, ValueRef>;
