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

    ref_a.construct<ReferenceType>(a, false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"a", ref_a}};
    b.construct<StructType>(std::move(fields_b));

    ref_b.construct<ReferenceType>(b, false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b}};
    a.construct<StructType>(std::move(fields_a));

    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
    EXPECT_EQ(a->compare_congruent(a, assumed_equal), std::strong_ordering::equal)
        << "Coinductively defined type should be equal to itself";
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
    TypeRegistry::add_ref_dependency(ref_a, a);
    TypeRegistry::get_at<ReferenceType>(ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields{{"a", ref_a}};
    TypeRegistry::get_at<StructType>(a, std::move(fields));

    // B -> &B -> B
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeRegistry::add_ref_dependency(ref_b, b);
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields2{{"a", ref_b}};
    TypeRegistry::get_at<StructType>(b, std::move(fields2));

    EXPECT_EQ(a.get(), b.get()) << "Recursive struct types should be interned";
}

TEST_F(TypeInterningTest, VerticalCongruenceIsRespected) {
    // A -> &B -> B -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(ref_a, a);
    TypeRegistry::get_at<ReferenceType>(ref_a, a, false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"a", ref_a}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));

    EXPECT_TRUE(TypeRegistry::is_type_incomplete(ref_a))
        << "ref_a should be incomplete before defining ref_b";
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false);

    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"a", ref_b}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &A -> A
    EXPECT_EQ(
        a->cast<StructType>()->fields_.at("a")->cast<ReferenceType>()->referenced_type_, a.get()
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
    TypeRegistry::add_ref_dependency(c_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", c_ref_a}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));
    TypeRegistry::get_at<ReferenceType>(ref_c, c, false);

    // D branch
    TypeRegistry::add_ref_dependency(d_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(d_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_d{{"a", d_ref_a}};
    TypeRegistry::get_at<StructType>(d, std::move(fields_d));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d));
    TypeRegistry::get_at<ReferenceType>(ref_d, d, false);

    // B
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"c", ref_c}, {"d", ref_d}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b));
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false);

    // A
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, where B has two fields both pointing to the
    // same C
    EXPECT_EQ(
        b->cast<StructType>()->fields_.at("c"),  //
        b->cast<StructType>()->fields_.at("d")
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
    TypeRegistry::add_ref_dependency(c_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", c_ref_a}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));
    TypeRegistry::get_at<ReferenceType>(ref_c, c, false);

    // D branch
    TypeRegistry::add_ref_dependency(e_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(e_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_e{{"a", e_ref_a}};
    TypeRegistry::get_at<StructType>(e, std::move(fields_e));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(e));
    TypeRegistry::get_at<ReferenceType>(ref_e, e, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_d{{"e", ref_e}};
    TypeRegistry::get_at<StructType>(d, std::move(fields_d));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d));
    TypeRegistry::get_at<ReferenceType>(ref_d, d, false);

    // B
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"c", ref_c}, {"d", ref_d}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b));
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false);

    // A
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, B -> &D -> D -> &C, where C and E are
    // congruent and should be interned, causing D's field to also point to C
    EXPECT_EQ(
        b->cast<StructType>()->fields_.at("c"),  //
        d->cast<StructType>()->fields_.at("e")
    ) << "Congruent struct types should be interned, causing their fields to be shared";
}

TEST_F(TypeInterningTest, PartialCompleteTypesAreNotInterned) {
    // A -> &B -> B -> &C -> C -> &A -> A
    // C -> &B -> B
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_c = std::type_identity<ReferenceType>();
    TypeResolution c = std::type_identity<StructType>();
    TypeResolution c_ref_b = std::type_identity<ReferenceType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(ref_a, a);
    TypeRegistry::add_ref_dependency(c_ref_b, b);

    TypeRegistry::get_at<ReferenceType>(ref_a, a, false);
    TypeRegistry::get_at<ReferenceType>(c_ref_b, b, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", ref_a}, {"b", c_ref_b}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));

    TypeRegistry::get_at<ReferenceType>(ref_c, c, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"c", ref_c}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));
}

TEST_F(TypeInterningTest, InterningBetweenBisimilarGraphsAtDifferenceRoot) {
    // A -> &B -> B -> &C -> C -> &A -> A
    // B -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_c = std::type_identity<ReferenceType>();
    TypeResolution c = std::type_identity<StructType>();
    TypeResolution c_ref_a = std::type_identity<ReferenceType>();
    TypeResolution b_ref_a = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(c_ref_a, a);
    TypeRegistry::add_ref_dependency(b_ref_a, a);

    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_c{{"a", c_ref_a}};
    TypeRegistry::get_at<StructType>(c, std::move(fields_c));

    TypeRegistry::get_at<ReferenceType>(ref_c, c, false);
    TypeRegistry::get_at<ReferenceType>(b_ref_a, a, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_b{{"c", ref_c}, {"a", b_ref_a}};
    TypeRegistry::get_at<StructType>(b, std::move(fields_b));

    TypeRegistry::get_at<ReferenceType>(ref_b, b, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_a{{"b", ref_b}};
    TypeRegistry::get_at<StructType>(a, std::move(fields_a));

    // D -> &E -> E -> &F -> F -> &D -> D
    // F -> &E -> E
    // D = C, E = A, F = B
    TypeResolution d = std::type_identity<StructType>();
    TypeResolution d_ref_e = std::type_identity<ReferenceType>();
    TypeResolution e = std::type_identity<StructType>();
    TypeResolution ref_f = std::type_identity<ReferenceType>();
    TypeResolution f = std::type_identity<StructType>();
    TypeResolution ref_d = std::type_identity<ReferenceType>();
    TypeResolution f_ref_e = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(ref_d, d);
    TypeRegistry::add_ref_dependency(f_ref_e, e);

    TypeRegistry::get_at<ReferenceType>(ref_d, d, false);
    TypeRegistry::get_at<ReferenceType>(f_ref_e, e, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_f{{"c", ref_d}, {"a", f_ref_e}};
    TypeRegistry::get_at<StructType>(f, std::move(fields_f));

    TypeRegistry::get_at<ReferenceType>(ref_f, f, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_e{{"b", ref_f}};
    TypeRegistry::get_at<StructType>(e, std::move(fields_e));

    TypeRegistry::get_at<ReferenceType>(d_ref_e, e, false);
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_d{{"a", d_ref_e}};
    TypeRegistry::get_at<StructType>(d, std::move(fields_d));

    EXPECT_EQ(
        a->cast<StructType>()
            ->fields_.at("b")
            ->cast<ReferenceType>()
            ->referenced_type_->cast<StructType>()
            ->fields_.at("c")
            ->cast<ReferenceType>()
            ->referenced_type_,
        d.get()
    ) << "Bisimilar types should be interned even if their roots are not identical";
}
