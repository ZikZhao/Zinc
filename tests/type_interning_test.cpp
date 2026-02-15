#include "pch.hpp"
#include "gtest/gtest.h"

#include "builtins.hpp"
#include "object.hpp"
#include "transpiler.hpp"

class TypeInterningTest : public ::testing::Test {
    friend class TypeResolution;

protected:
    void SetUp() override { TypeRegistry::instance.emplace(); }
    void TearDown() override { TypeRegistry::instance.reset(); }
};

TEST_F(TypeInterningTest, CoinductionComparisonWorks) {
    // A -> &B -> B -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();

    ref_a.construct<ReferenceType>(a.get(), false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"a", ref_a.get()}};
    b.construct<StructType>(std::move(fields_b));

    ref_b.construct<ReferenceType>(b.get(), false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b.get()}};
    a.construct<StructType>(std::move(fields_a));

    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
    EXPECT_EQ(a.get()->compare_congruent(a.get(), assumed_equal), std::strong_ordering::equal)
        << "Coinductively defined type should be equal to itself";
}

TEST_F(TypeInterningTest, SimpleArrayTypesAreInterned) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* array1 = TypeRegistry::get<ArrayType>(int_type);
    const auto* array2 = TypeRegistry::get<ArrayType>(int_type);
    EXPECT_EQ(array1, array2) << "Array types with same element type should be interned";
    EXPECT_EQ(array1->element_type_, int_type);
}

TEST_F(TypeInterningTest, DifferentArrayTypesAreDifferent) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* float_type = &FloatType::untyped_instance;
    const auto* int_array = TypeRegistry::get<ArrayType>(int_type);
    const auto* float_array = TypeRegistry::get<ArrayType>(float_type);
    EXPECT_NE(int_array, float_array)
        << "Array types with different element types should be different";
}

TEST_F(TypeInterningTest, SimpleFunctionTypesAreInterned) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* float_type = &FloatType::untyped_instance;
    const Type* t1 = int_type;
    const Type* t2 = float_type;
    auto params1 = GlobalMemory::pack_array(t1, t2);
    auto params2 = GlobalMemory::pack_array(t1, t2);
    const auto* func1 = TypeRegistry::get<FunctionType>(params1, int_type);
    const auto* func2 = TypeRegistry::get<FunctionType>(params2, int_type);
    EXPECT_EQ(func1, func2) << "Function types with same signature should be interned";
}

TEST_F(TypeInterningTest, DifferentFunctionTypesAreDifferent) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* float_type = &FloatType::untyped_instance;
    const Type* t1 = int_type;
    auto params = GlobalMemory::pack_array(t1);
    const auto* func1 = TypeRegistry::get<FunctionType>(params, int_type);
    const auto* func2 = TypeRegistry::get<FunctionType>(params, float_type);
    EXPECT_NE(func1, func2) << "Function types with different return types should be different";
    const Type* t2 = float_type;
    auto more_params = GlobalMemory::pack_array(t1, t2);
    const auto* func3 = TypeRegistry::get<FunctionType>(more_params, int_type);
    EXPECT_NE(func1, func3) << "Function types with different parameter counts should be different";
}

TEST_F(TypeInterningTest, ReferenceTypesAreInterned) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* ref1 = TypeRegistry::get<ReferenceType>(int_type, false);
    const auto* ref2 = TypeRegistry::get<ReferenceType>(int_type, false);
    EXPECT_EQ(ref1, ref2) << "Reference types to same type should be interned";
    const auto* mut_ref1 = TypeRegistry::get<ReferenceType>(int_type, true);
    const auto* mut_ref2 = TypeRegistry::get<ReferenceType>(int_type, true);
    EXPECT_EQ(mut_ref1, mut_ref2) << "Mutable reference types should be interned";
    EXPECT_NE(ref1, mut_ref1) << "Mutable and immutable references should be different";
}

TEST_F(TypeInterningTest, SimpleStructTypesAreInterned) {
    const auto* int_type = &IntegerType::untyped_instance;
    const auto* float_type = &FloatType::untyped_instance;
    GlobalMemory::FlatMap<std::string_view, const Type*> fields1;
    fields1.insert({"x", int_type});
    fields1.insert({"y", float_type});
    GlobalMemory::FlatMap<std::string_view, const Type*> fields2;
    fields2.insert({"x", int_type});
    fields2.insert({"y", float_type});
    const auto* struct1 = TypeRegistry::get<StructType>(std::move(fields1));
    const auto* struct2 = TypeRegistry::get<StructType>(std::move(fields2));
    EXPECT_EQ(struct1, struct2) << "Struct types with same fields should be interned";
}

TEST_F(TypeInterningTest, RecursiveTypesAreInterned) {
    // A -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();
    TypeRegistry::add_ref_dependency(ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(ref_a, a.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields{{"a", ref_a.get()}};
    TypeRegistry::get_at<StructType>(a, std::move(fields));

    // B -> &B -> B
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeRegistry::add_ref_dependency(ref_b.get(), b.get());
    TypeRegistry::get_at<ReferenceType>(ref_b, b.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields2{{"a", ref_b.get()}};
    TypeRegistry::get_at<StructType>(b, std::move(fields2));

    EXPECT_EQ(a.get(), b.get()) << "Recursive struct types should be interned";
}

TEST_F(TypeInterningTest, VerticalCongruenceIsRespected) {
    // A -> &B -> B -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(ref_a, a.get(), false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"a", ref_a.get()}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));

    EXPECT_TRUE(TypeRegistry::is_type_incomplete(ref_a.get()))
        << "ref_a should be incomplete before defining ref_b";
    TypeRegistry::get_at<ReferenceType>(ref_b, b.get(), false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"a", ref_b.get()}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &A -> A
    EXPECT_EQ(
        a.get(),
        a.get()->cast<StructType>()->fields_.at("a")->cast<ReferenceType>()->referenced_type_
    ) << "Inner reference type should point to the interned struct type";
}

TEST_F(TypeInterningTest, HorizontalCongruenceIsRespected) {
    // A -> &B -> B
    // B -> &C -> C -> &A -> A
    // B -> &D -> D -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_c = std::type_identity<ReferenceType>();
    TypeResolution c = std::type_identity<StructType>();
    TypeResolution ref_d = std::type_identity<ReferenceType>();
    TypeResolution d = std::type_identity<StructType>();
    TypeResolution c_ref_a = std::type_identity<ReferenceType>();
    TypeResolution d_ref_a = std::type_identity<ReferenceType>();

    // C branch
    TypeRegistry::add_ref_dependency(c_ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", c_ref_a.get()}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c.get()));
    TypeRegistry::get_at<ReferenceType>(ref_c, c.get(), false);

    // D branch
    TypeRegistry::add_ref_dependency(d_ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(d_ref_a, a.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_d{{"a", d_ref_a.get()}};
    TypeRegistry::get_at<StructType>(d, std::move(fields_d));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d.get()));
    TypeRegistry::get_at<ReferenceType>(ref_d, d.get(), false);

    // B
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{
        {"c", ref_c.get()}, {"d", ref_d.get()}
    };
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b.get()));
    TypeRegistry::get_at<ReferenceType>(ref_b, b.get(), false);

    // A
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b.get()}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, where B has two fields both pointing to the
    // same C
    EXPECT_EQ(
        b.get()->cast<StructType>()->fields_.at("c"),  //
        b.get()->cast<StructType>()->fields_.at("d")
    ) << "Congruent struct types should be interned, causing their fields to be shared";
}

TEST_F(TypeInterningTest, IrregularHorizontalCongruenceIsRespected) {
    // A -> &B -> B
    // B -> &C -> C -> &A -> A
    // B -> &D -> D -> &E -> E -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_c = std::type_identity<ReferenceType>();
    TypeResolution c = std::type_identity<StructType>();
    TypeResolution c_ref_a = std::type_identity<ReferenceType>();
    TypeResolution ref_d = std::type_identity<ReferenceType>();
    TypeResolution d = std::type_identity<StructType>();
    TypeResolution ref_e = std::type_identity<ReferenceType>();
    TypeResolution e = std::type_identity<StructType>();
    TypeResolution e_ref_a = std::type_identity<ReferenceType>();

    // C branch
    TypeRegistry::add_ref_dependency(c_ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", c_ref_a.get()}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c.get()));
    TypeRegistry::get_at<ReferenceType>(ref_c, c.get(), false);

    // D branch
    TypeRegistry::add_ref_dependency(e_ref_a.get(), a.get());
    TypeRegistry::get_at<ReferenceType>(e_ref_a, a.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_e{{"a", e_ref_a.get()}};
    TypeRegistry::get_at<StructType>(e, std::move(fields_e));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(e.get()));
    TypeRegistry::get_at<ReferenceType>(ref_e, e.get(), false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_d{{"e", ref_e.get()}};
    TypeRegistry::get_at<StructType>(d, std::move(fields_d));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d.get()));
    TypeRegistry::get_at<ReferenceType>(ref_d, d.get(), false);

    // B
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{
        {"c", ref_c.get()}, {"d", ref_d.get()}
    };
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b.get()));
    TypeRegistry::get_at<ReferenceType>(ref_b, b.get(), false);

    // A
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b.get()}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, B -> &D -> D -> &C, where C and E are
    // congruent and should be interned, causing D's field to also point to C
    EXPECT_EQ(
        b.get()->cast<StructType>()->fields_.at("c"),  //
        d.get()->cast<StructType>()->fields_.at("e")
    ) << "Congruent struct types should be interned, causing their fields to be shared";
}
