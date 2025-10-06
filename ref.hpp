#pragma once
#include "pch.hpp"

template<typename TargetType>
class Reference {
private:
    std::shared_ptr<TargetType> ptr;
    Reference* source;
public:
    Reference() = default;
    Reference(const Reference& other) = default;
    Reference(Reference&& other) = default;
    Reference(TargetType* ptr) : ptr(ptr), source(nullptr) {}
    Reference(std::shared_ptr<TargetType>&& other) : ptr(std::forward<std::shared_ptr<TargetType>>(other)), source(nullptr) {}
    // Copy constructor that keeps track of source for assignment propagation
    Reference(Reference& other) : ptr(other.ptr), source(&other) {}
public:
    // Assignment operator with adaptation
    Reference& operator = (const Reference& other) {
        if (TargetType* adapted = this->ptr->adapt_for_assignment(*other.ptr)) {
            this->ptr = std::shared_ptr<TargetType>(adapted);
        } else {
            this->ptr = other.ptr;
        }
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
    Reference operator () (auto&&... args) const {
        return this->operator*()(std::forward<decltype(args)>(args)...);
    }
    Reference operator [] (auto&& index) const {
        return this->operator*()[std::forward<decltype(index)>(index)];
    }
    bool is_truthy() const {
        return this->operator*().is_truthy();
    }
    bool contains(const TargetType& other) const {
        return this->operator*().contains(other);
    }
};
