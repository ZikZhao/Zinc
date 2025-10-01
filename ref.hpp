#pragma once
#include <stdexcept>
#include <vector>
#include <string>
#include <memory>

class Value;
class Type;
template<typename TargetType> class Reference;

using ValueRef = Reference<Value>;
using TypeRef = Reference<Type>;

template<typename TargetType>
class Reference {
    friend class Variable;
private:
    std::shared_ptr<TargetType> ptr;
    Reference(TargetType* ptr) : ptr(ptr) {}
public:
    Reference(std::shared_ptr<TargetType>&& other) : ptr(std::forward<std::shared_ptr<TargetType>>(other)) {}
    Reference(const Reference& other) : ptr(other.ptr) {}
    Reference& operator = (const Reference& other) {
        ptr = other.ptr;
        return *this;
    }
    TargetType& operator * () const noexcept {
        return *ptr;
    }
    template<typename U = TargetType, typename = std::enable_if_t<std::is_same_v<U, Value>>>
    TypeRef type() const noexcept {
        return (*ptr).type;
    }
    operator std::string () const {
        return static_cast<std::string>(operator*());
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
};
