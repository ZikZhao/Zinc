#include "pch.hpp"
#include "gtest/gtest.h"

#include "object.hpp"

namespace {

auto make_fields(std::initializer_list<std::pair<strview, const Type*>> fields)
    -> GlobalMemory::Vector<std::pair<strview, const Type*>> {
    return {fields.begin(), fields.end()};
}

auto find_field(const StructType* struct_type, strview name) -> const Type* {
    auto it =
        std::ranges::find(struct_type->fields_, name, &std::pair<strview, const Type*>::first);
    return it == struct_type->fields_.end() ? nullptr : it->second;
}

}  // namespace

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

    TypeRegistry::get_at<ReferenceType>(ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(b, make_fields({{"a", ref_a}}));

    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);
    TypeRegistry::get_at<StructType>(a, make_fields({{"b", ref_b}}));

    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
    EXPECT_EQ(a->compare(a, assumed_equal), std::strong_ordering::equal)
        << "Coinductively defined type should be equal to itself";
}

TEST_F(TypeInterningTest, ReferenceTypesAreInterned) {
    const auto* int_type = &IntegerType::i32_instance;
    const auto* ref1 = TypeRegistry::get<ReferenceType>(int_type, false, false);
    const auto* ref2 = TypeRegistry::get<ReferenceType>(int_type, false, false);
    EXPECT_EQ(ref1, ref2) << "Reference types to same type should be interned";
}

TEST_F(TypeInterningTest, SimpleStructTypesAreInterned) {
    const auto* int_type = &IntegerType::i32_instance;
    const auto* float_type = &FloatType::f32_instance;
    const auto* struct1 =
        TypeRegistry::get<StructType>(make_fields({{"x", int_type}, {"y", float_type}}));
    const auto* struct2 =
        TypeRegistry::get<StructType>(make_fields({{"x", int_type}, {"y", float_type}}));
    EXPECT_EQ(struct1, struct2) << "Struct types with same fields should be interned";
}

TEST_F(TypeInterningTest, RecursiveTypesAreInterned) {
    // A -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();
    TypeRegistry::add_ref_dependency(ref_a, a);
    TypeRegistry::get_at<ReferenceType>(ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(a, make_fields({{"a", ref_a}}));

    // B -> &B -> B
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);
    TypeRegistry::get_at<StructType>(b, make_fields({{"a", ref_b}}));

    EXPECT_EQ(a.get(), b.get()) << "Recursive struct types should be interned";
}

TEST_F(TypeInterningTest, VerticalCongruenceIsRespected) {
    // A -> &B -> B -> &A -> A
    TypeResolution a = std::type_identity<StructType>();
    TypeResolution ref_b = std::type_identity<ReferenceType>();
    TypeResolution b = std::type_identity<StructType>();
    TypeResolution ref_a = std::type_identity<ReferenceType>();

    TypeRegistry::add_ref_dependency(ref_a, a);
    TypeRegistry::get_at<ReferenceType>(ref_a, a, false, false);

    TypeRegistry::get_at<StructType>(b, make_fields({{"a", ref_a}}));

    EXPECT_TRUE(TypeRegistry::is_type_incomplete(ref_a))
        << "ref_a should be incomplete before defining ref_b";
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);

    TypeRegistry::get_at<StructType>(a, make_fields({{"a", ref_b}}));

    // Simplify to A -> &A -> A
    const Type* a_field = find_field(a->cast<StructType>(), "a");
    ASSERT_NE(a_field, nullptr);
    EXPECT_EQ(a_field->cast<ReferenceType>()->target_type_, a.get())
        << "Inner reference type should point to the interned struct type";
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
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(c, make_fields({{"a", c_ref_a}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));
    TypeRegistry::get_at<ReferenceType>(ref_c, c, false, false);

    // D branch
    TypeRegistry::add_ref_dependency(d_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(d_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(d, make_fields({{"a", d_ref_a}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d));
    TypeRegistry::get_at<ReferenceType>(ref_d, d, false, false);

    // B
    TypeRegistry::get_at<StructType>(b, make_fields({{"c", ref_c}, {"d", ref_d}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b));
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);

    // A
    TypeRegistry::get_at<StructType>(a, make_fields({{"b", ref_b}}));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, where B has two fields both pointing to the
    // same C
    EXPECT_EQ(find_field(b->cast<StructType>(), "c"), find_field(b->cast<StructType>(), "d"))
        << "Congruent struct types should be interned, causing their fields to be shared";
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
    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(c, make_fields({{"a", c_ref_a}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));
    TypeRegistry::get_at<ReferenceType>(ref_c, c, false, false);

    // D branch
    TypeRegistry::add_ref_dependency(e_ref_a, a);
    TypeRegistry::get_at<ReferenceType>(e_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(e, make_fields({{"a", e_ref_a}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(e));
    TypeRegistry::get_at<ReferenceType>(ref_e, e, false, false);
    TypeRegistry::get_at<StructType>(d, make_fields({{"e", ref_e}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(d));
    TypeRegistry::get_at<ReferenceType>(ref_d, d, false, false);

    // B
    TypeRegistry::get_at<StructType>(b, make_fields({{"c", ref_c}, {"d", ref_d}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(b));
    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);

    // A
    TypeRegistry::get_at<StructType>(a, make_fields({{"b", ref_b}}));

    // Simplify to A -> &B -> B -> &C -> C -> &A -> A, B -> &D -> D -> &C, where C and E are
    // congruent and should be interned, causing D's field to also point to C
    EXPECT_EQ(find_field(b->cast<StructType>(), "c"), find_field(d->cast<StructType>(), "e"))
        << "Congruent struct types should be interned, causing their fields to be shared";
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

    TypeRegistry::get_at<ReferenceType>(ref_a, a, false, false);
    TypeRegistry::get_at<ReferenceType>(c_ref_b, b, false, false);
    TypeRegistry::get_at<StructType>(c, make_fields({{"a", ref_a}, {"b", c_ref_b}}));
    EXPECT_TRUE(TypeRegistry::is_type_incomplete(c));

    TypeRegistry::get_at<ReferenceType>(ref_c, c, false, false);
    TypeRegistry::get_at<StructType>(b, make_fields({{"c", ref_c}}));
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

    TypeRegistry::get_at<ReferenceType>(c_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(c, make_fields({{"a", c_ref_a}}));

    TypeRegistry::get_at<ReferenceType>(ref_c, c, false, false);
    TypeRegistry::get_at<ReferenceType>(b_ref_a, a, false, false);
    TypeRegistry::get_at<StructType>(b, make_fields({{"c", ref_c}, {"a", b_ref_a}}));

    TypeRegistry::get_at<ReferenceType>(ref_b, b, false, false);
    TypeRegistry::get_at<StructType>(a, make_fields({{"b", ref_b}}));

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

    TypeRegistry::get_at<ReferenceType>(ref_d, d, false, false);
    TypeRegistry::get_at<ReferenceType>(f_ref_e, e, false, false);
    TypeRegistry::get_at<StructType>(f, make_fields({{"c", ref_d}, {"a", f_ref_e}}));

    TypeRegistry::get_at<ReferenceType>(ref_f, f, false, false);
    TypeRegistry::get_at<StructType>(e, make_fields({{"b", ref_f}}));

    TypeRegistry::get_at<ReferenceType>(d_ref_e, e, false, false);
    TypeRegistry::get_at<StructType>(d, make_fields({{"a", d_ref_e}}));

    const Type* b_field = find_field(a->cast<StructType>(), "b");
    ASSERT_NE(b_field, nullptr);
    const Type* c_field =
        find_field(b_field->cast<ReferenceType>()->target_type_->cast<StructType>(), "c");
    ASSERT_NE(c_field, nullptr);
    EXPECT_EQ(c_field->cast<ReferenceType>()->target_type_, d.get())
        << "Bisimilar types should be interned even if their roots are not identical";
}
