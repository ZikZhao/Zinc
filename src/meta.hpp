#pragma once
#include "pch.hpp"

#include "object.hpp"

using MetaFunction = const Object* (*)(std::span<const Object*>);

namespace Meta {

template <typename T>
struct MetaTraits;

template <typename R, typename... Args>
struct MetaTraits<R (*)(Args...)> {
    static_assert(
        (std::convertible_to<Args, const Object*> && ...),
        "Meta function parameters must be const Object*"
    );

    using ArgTypes = std::tuple<Args...>;

    template <size_t index>
    using ArgType = std::tuple_element_t<index, ArgTypes>;

    using IndexSequence = std::make_index_sequence<sizeof...(Args)>;

    static auto validate(std::span<const Object*> args) -> void {
        if (args.size() != sizeof...(Args)) {
            throw std::invalid_argument("Incorrect number of arguments for meta function");
        }
        ([&]<size_t... indices>(std::integer_sequence<size_t, indices...>) {
            (validate_arg<indices>(args[indices]), ...);
        }(IndexSequence{}));
    }

    template <size_t index>
    static auto validate_arg(const Object* obj) -> void {
        if constexpr (std::is_same_v<ArgType<index>, const Type*>) {
            if (!obj->dyn_type()) {
                throw std::invalid_argument("Expected a type argument for meta function");
            }
        } else {
            if (!obj->dyn_value()) {
                throw std::invalid_argument("Expected a value argument for meta function");
            }
        }
    }
};

template <auto Fn>
struct MetaWrapper {
    static auto invoke(std::span<const Object*> args) -> const Object* {
        using Traits = MetaTraits<decltype(Fn)>;
        Traits::validate(args);
        auto result = ([&]<size_t... indices>(std::integer_sequence<size_t, indices...>) {
            return Fn(static_cast<Traits::template ArgType<indices>>(args[indices])...);
        }(typename Traits::IndexSequence{}));
        if constexpr (std::is_same_v<decltype(result), bool>) {
            return new BooleanValue(result);
        } else {
            return result;
        }
    }
};

// auto is_void(const Type* type) { return type->kind_ == Kind::Void; }

auto is_integral(const Type* type) -> bool { return type->kind_ == Kind::Integer; }

auto is_floating_point(const Type* type) -> bool { return type->kind_ == Kind::Float; }

auto is_array(const Type* type) -> bool { return type->kind_ == Kind::Array; }

auto is_pointer(const Type* type) -> bool { return type->kind_ == Kind::Pointer; }

auto is_lvalue_reference(const Type* type) -> bool {
    return type->kind_ == Kind::Reference && !type->cast<ReferenceType>()->is_moved_;
}

auto is_rvalue_reference(const Type* type) -> bool {
    return type->kind_ == Kind::Reference && type->cast<ReferenceType>()->is_moved_;
}

// auto is_member_object_pointer(const Type* type) {
//     return type->kind_ == Kind::MemberObjectPointer;
// }

// auto is_member_function_pointer(const Type* type) {
//     return type->kind_ == Kind::Function && type->cast<FunctionType>()->is_member_function_;
// }

// auto is_enum(const Type* type) { return type->kind_ == Kind::Enum; }

auto is_class(const Type* type) -> bool { return type->kind_ == Kind::Instance; }

auto is_function(const Type* type) -> bool { return type->kind_ == Kind::Function; }

auto is_reference(const Type* type) -> bool { return type->kind_ == Kind::Reference; }

auto is_arithmetic(const Type* type) -> bool {
    return type->kind_ == Kind::Integer || type->kind_ == Kind::Float;
}

auto is_fundamental(const Type* type) -> bool {
    return is_arithmetic(type) || type->kind_ == Kind::Boolean;
}

auto is_scalar(const Type* type) -> bool {
    return is_fundamental(type) || type->kind_ == Kind::Pointer;
}

auto is_object(const Type* type) -> bool {
    return is_scalar(type) || type->kind_ == Kind::Array || type->kind_ == Kind::Union ||
           type->kind_ == Kind::Instance;
}

auto is_compound(const Type* type) -> bool { return !is_scalar(type); }

// auto is_member_pointer(const Type* type) -> bool {
//     return (type->kind_ == Kind::Pointer && type->cast<PointerType>()->inner_->kind_ ==
//     Kind::Instance) ||
//            (type->kind_ == Kind::Reference && type->cast<ReferenceType>()->inner_->kind_ ==
//            Kind::Instance);
// }

auto is_mut(const Type* type) -> bool { return type->kind_ == Kind::Mutable; }

auto is_const(const Type* type) -> bool { return !is_mut(type); }

auto get_metas() -> std::generator<std::pair<std::string_view, MetaFunction>> {
    // scope.add_meta("is_void", MetaWrapper<is_void>{});
    co_yield {"is_integral", MetaWrapper<is_integral>::invoke};
    co_yield {"is_floating_point", MetaWrapper<is_floating_point>::invoke};
    co_yield {"is_array", MetaWrapper<is_array>::invoke};
    co_yield {"is_pointer", MetaWrapper<is_pointer>::invoke};
    co_yield {"is_lvalue_reference", MetaWrapper<is_lvalue_reference>::invoke};
    co_yield {"is_rvalue_reference", MetaWrapper<is_rvalue_reference>::invoke};
    // co_yield {"is_member_object_pointer", MetaWrapper<is_member_object_pointer>::invoke};
    // co_yield {"is_member_function_pointer", MetaWrapper<is_member_function_pointer>::invoke};
    // co_yield {"is_enum", MetaWrapper<is_enum>::invoke};
    co_yield {"is_class", MetaWrapper<is_class>::invoke};
    co_yield {"is_function", MetaWrapper<is_function>::invoke};

    co_yield {"is_reference", MetaWrapper<is_reference>::invoke};
    co_yield {"is_arithmetic", MetaWrapper<is_arithmetic>::invoke};
    co_yield {"is_fundamental", MetaWrapper<is_fundamental>::invoke};
    co_yield {"is_scalar", MetaWrapper<is_scalar>::invoke};
    co_yield {"is_object", MetaWrapper<is_object>::invoke};
    co_yield {"is_compound", MetaWrapper<is_compound>::invoke};
    co_yield {"is_mut", MetaWrapper<is_mut>::invoke};
    co_yield {"is_const", MetaWrapper<is_const>::invoke};
}
}  // namespace Meta
