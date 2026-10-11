#include "pch.hpp"

#include <cstdint>
#include <forward_list>
#include <sstream>
#include <variant>

#include "gtest/gtest.h"

namespace {

using Owned = std::unique_ptr<int>;
static_assert(std::ranges::contiguous_range<Array<int>>);
static_assert(std::ranges::contiguous_range<const Array<int>>);
static_assert(std::ranges::sized_range<Array<int>>);
static_assert(!std::ranges::view<Array<int>>);
static_assert(!std::ranges::borrowed_range<Array<int>>);
static_assert(std::is_convertible_v<Array<int>&, std::span<int>>);
static_assert(std::is_convertible_v<Array<int>&, std::span<const int>>);
static_assert(std::is_convertible_v<const Array<int>&, std::span<const int>>);
static_assert(!std::is_convertible_v<const Array<int>&, std::span<int>>);
static_assert(!std::is_convertible_v<std::span<int>, Array<int>>);
static_assert(!std::is_convertible_v<std::span<const int>, Array<int>>);
static_assert(std::is_constructible_v<Array<int>, std::span<const int, 3>>);
static_assert(std::is_nothrow_move_constructible_v<Array<Owned>>);
static_assert(std::is_nothrow_move_assignable_v<Array<Owned>>);
static_assert(std::is_same_v<decltype(std::span(std::declval<Array<int>&>())), std::span<int>>);
static_assert(std::is_same_v<decltype(std::declval<const Array<int>&>()[0]), const int&>);
static_assert(std::is_same_v<decltype(std::declval<const Array<int>&>().data()), const int*>);
static_assert(
    std::is_same_v<decltype(std::declval<const Array<int>&>().first<2>()), std::span<const int, 2>>
);

// Array may be a member while its element type is still incomplete.
struct RecursiveElement;
struct RecursiveOwner {
    Array<RecursiveElement> elements;
};
struct RecursiveElement {
    std::unique_ptr<RecursiveOwner> child;
};

struct Lifetime {
    static inline int live = 0;
    static inline int copies_left = -1;
    int value = 0;

    explicit Lifetime(int input = 0) : value(input) { ++live; }
    Lifetime(const Lifetime& other) : value(other.value) {
        if (copies_left == 0) {
            throw std::runtime_error("copy failed");
        }
        if (copies_left > 0) {
            --copies_left;
        }
        ++live;
    }
    Lifetime(Lifetime&& other) noexcept(false) : value(other.value) {
        other.value = -1;
        ++live;
    }
    Lifetime& operator=(const Lifetime&) = delete;
    Lifetime& operator=(Lifetime&&) = delete;
    ~Lifetime() { --live; }
};

struct SizedInput {
    struct Iterator {
        using value_type = int;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;
        SizedInput* input = nullptr;
        int operator*() const { return input->values[input->position]; }
        Iterator& operator++() {
            ++input->position;
            if (input->position == input->throw_at) {
                throw std::runtime_error("increment failed");
            }
            return *this;
        }
        void operator++(int) { ++*this; }
        bool operator==(std::default_sentinel_t) const {
            return input->position == input->values.size();
        }
    };
    std::array<int, 3> values{1, 2, 3};
    std::size_t position = 0;
    std::size_t throw_at = std::numeric_limits<std::size_t>::max();
    int begin_calls = 0;
    Iterator begin() {
        ++begin_calls;
        return {this};
    }
    std::default_sentinel_t end() const { return {}; }
    std::size_t size() const { return values.size(); }
};
static_assert(std::ranges::input_range<SizedInput>);
static_assert(std::ranges::sized_range<SizedInput>);
static_assert(!std::ranges::forward_range<SizedInput>);

struct Immovable {
    int value;
    explicit Immovable(int input) : value(input) {}
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
};

TEST(ArrayTest, EmptyArraysAndZeroLengthViews) {
    Array<int> values(0);
    auto copy = values;
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(copy.empty());
    EXPECT_EQ(values.data(), nullptr);
    EXPECT_EQ(values.begin(), values.end());
    EXPECT_EQ(values.rbegin(), values.rend());
    EXPECT_EQ(values.first<0>().size(), 0);
    EXPECT_EQ(values.last(0).size(), 0);
    EXPECT_EQ(values.subspan(0).size(), 0);
    EXPECT_EQ(values.size_bytes(), 0);
    EXPECT_THROW(values.at(0), std::out_of_range);
}

TEST(ArrayTest, CountConstructionValueInitializesAndFillCopies) {
    Array<int> zeroes(4);
    EXPECT_TRUE(std::ranges::all_of(zeroes, [](int value) { return value == 0; }));
    Array<std::string> repeated(3, "value");
    ASSERT_EQ(repeated.size(), 3);
    EXPECT_EQ(repeated.back(), "value");
    EXPECT_NE(repeated[0].data(), repeated[1].data());
}

TEST(ArrayTest, SpanConversionSubviewsAndConstness) {
    Array values{1, 2, 3, 4};
    std::span<int> mutable_view = values;
    std::span<const int> immutable_view = std::as_const(values);
    EXPECT_EQ(mutable_view.data(), values.data());
    EXPECT_EQ(immutable_view.data(), values.data());
    mutable_view[1] = 20;
    EXPECT_EQ(values[1], 20);
    EXPECT_EQ(values.size_bytes(), 4 * sizeof(int));
    EXPECT_EQ(values.first<2>().size(), 2);
    EXPECT_EQ(values.last<2>().front(), 3);
    EXPECT_EQ(values.first(1).front(), 1);
    EXPECT_EQ(values.last(1).front(), 4);
    EXPECT_EQ((values.subspan<1, 2>().back()), 3);
    EXPECT_EQ(values.subspan(2).size(), 2);
    EXPECT_EQ(values.subspan(1, 2).data(), values.data() + 1);
    EXPECT_EQ(std::as_const(values).first(2).back(), 20);
    EXPECT_EQ(std::as_const(values).last<1>().front(), 4);
    EXPECT_EQ(std::as_const(values).subspan<1>().size(), 3);
    EXPECT_EQ(std::as_const(values).subspan(1, 2).back(), 3);
    EXPECT_EQ(*values.rbegin(), 4);
    EXPECT_EQ(*values.crbegin(), 4);
    EXPECT_EQ(std::as_const(values).cend() - values.cbegin(), 4);
    EXPECT_THROW(std::as_const(values).at(4), std::out_of_range);
}

TEST(ArrayTest, ExplicitSpanConstructionCopiesAndSupportsStaticExtent) {
    int raw[]{1, 2, 3};
    Array copy(std::span<const int, 3>{raw});
    static_assert(std::is_same_v<decltype(copy), Array<int>>);
    raw[0] = 9;
    EXPECT_EQ(copy.front(), 1);
    std::span<int, 3> fixed(copy);
    EXPECT_EQ(fixed.data(), copy.data());
    std::span<int> view(copy);
    Array second(view);
    EXPECT_NE(second.data(), copy.data());
}

TEST(ArrayTest, CopiesConstructIndependentElementsWithoutAssignment) {
    ASSERT_EQ(Lifetime::live, 0);
    {
        Array<Lifetime> original(3);
        original[1].value = 17;
        Array copy(original);
        EXPECT_EQ(Lifetime::live, 6);
        EXPECT_NE(original.data(), copy.data());
        EXPECT_EQ(copy[1].value, 17);
        copy[1].value = 99;
        EXPECT_EQ(original[1].value, 17);
        Array<Lifetime> target(1);
        target = original;
        EXPECT_EQ(target.size(), 3);
        EXPECT_EQ(target[1].value, 17);
        EXPECT_EQ(Lifetime::live, 9);
        auto* storage = original.data();
        auto& alias = original;
        original = alias;
        EXPECT_EQ(original.data(), storage);
    }
    EXPECT_EQ(Lifetime::live, 0);
}

TEST(ArrayTest, MovesTransferStorageAndReleasePreviousElements) {
    ASSERT_EQ(Lifetime::live, 0);
    {
        Array<Lifetime> original(3);
        auto* storage = original.data();
        Array moved(std::move(original));
        EXPECT_TRUE(original.empty());
        EXPECT_EQ(original.data(), nullptr);
        EXPECT_EQ(moved.data(), storage);
        Array<Lifetime> target(2);
        target = std::move(moved);
        EXPECT_EQ(target.data(), storage);
        EXPECT_TRUE(moved.empty());
        EXPECT_EQ(Lifetime::live, 3);
        auto& alias = target;
        target = std::move(alias);
        EXPECT_EQ(target.data(), storage);
    }
    EXPECT_EQ(Lifetime::live, 0);
}

TEST(ArrayTest, MoveOnlyVariantElementsAreDestroyed) {
    ASSERT_EQ(Lifetime::live, 0);
    using Node = std::variant<std::monostate, std::unique_ptr<Lifetime>>;
    {
        auto generated = std::views::iota(0, 4) | std::views::transform([](int value) {
                             return Node{std::make_unique<Lifetime>(value)};
                         });
        auto values = generated | std::ranges::to<Array>();
        static_assert(std::is_same_v<decltype(values), Array<Node>>);
        EXPECT_EQ(values.size(), 4);
        EXPECT_EQ(Lifetime::live, 4);
        EXPECT_EQ(std::get<1>(values[3])->value, 3);
        auto moved = std::move(values);
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(Lifetime::live, 4);
    }
    EXPECT_EQ(Lifetime::live, 0);
}

TEST(ArrayTest, IteratorPairsAndMovedRangesSupportMoveOnlyElements) {
    std::vector<Owned> source;
    source.push_back(std::make_unique<int>(5));
    source.push_back(std::make_unique<int>(6));
    Array values(std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
    EXPECT_EQ(*values.front(), 5);
    EXPECT_EQ(source[0], nullptr);
    Array<Owned> moved(std::from_range, values | std::views::as_rvalue);
    EXPECT_EQ(*moved.back(), 6);
    EXPECT_EQ(values[1], nullptr);
}

TEST(ArrayTest, SinglePassInputIsConsumedOnce) {
    std::istringstream stream("10 20 30 40");
    Array values(std::from_range, std::ranges::istream_view<int>(stream));
    EXPECT_TRUE(std::ranges::equal(values, Array{10, 20, 30, 40}));
    std::istringstream second_stream("3 2 1");
    Array from_iterators(std::istream_iterator<int>{second_stream}, std::istream_iterator<int>{});
    EXPECT_TRUE(std::ranges::equal(from_iterators, Array{3, 2, 1}));
}

TEST(ArrayTest, SinglePassBooleanInputStillProducesContiguousBools) {
    std::istringstream stream("1 0 1");
    Array values(std::from_range, std::ranges::istream_view<bool>(stream));
    static_assert(std::is_same_v<decltype(values.data()), bool*>);
    ASSERT_EQ(values.size(), 3);
    EXPECT_TRUE(values[0]);
    EXPECT_FALSE(values[1]);
    std::span<bool> view = values;
    EXPECT_EQ(view.data(), values.data());
}

TEST(ArrayTest, ForwardAndSizedSinglePassRanges) {
    std::forward_list<int> source{1, 2, 3};
    Array values(std::from_range, source);
    EXPECT_TRUE(std::ranges::equal(values, source));
    SizedInput input;
    Array second(std::from_range, input);
    EXPECT_EQ(input.begin_calls, 1);
    EXPECT_EQ(input.position, 3);
    EXPECT_TRUE(std::ranges::equal(second, input.values));
}

TEST(ArrayTest, IteratorIncrementFailureDestroysTheLastConstructedElement) {
    ASSERT_EQ(Lifetime::live, 0);
    SizedInput input;
    input.throw_at = 2;
    EXPECT_THROW((Array<Lifetime>(std::from_range, input)), std::runtime_error);
    EXPECT_EQ(Lifetime::live, 0);
}

TEST(ArrayTest, PartialConstructionAndFailedCopyAssignmentCleanUp) {
    ASSERT_EQ(Lifetime::live, 0);
    {
        Array<Lifetime> source(3);
        Array<Lifetime> target(1);
        target[0].value = 42;
        auto* storage = target.data();
        Lifetime::copies_left = 1;
        EXPECT_THROW(target = source, std::runtime_error);
        Lifetime::copies_left = -1;
        EXPECT_EQ(target.data(), storage);
        EXPECT_EQ(target.size(), 1);
        EXPECT_EQ(target.front().value, 42);
        EXPECT_EQ(Lifetime::live, 4);
    }
    EXPECT_EQ(Lifetime::live, 0);
}

TEST(ArrayTest, SizeOverflowIsRejectedBeforeAllocation) {
    EXPECT_THROW((Array<int>(std::numeric_limits<std::size_t>::max())), std::length_error);
}

TEST(ArrayTest, OveralignedElementsWork) {
    struct alignas(128) Aligned {
        int value = 0;
    };
    Array<Aligned> aligned(3);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned.data()) % alignof(Aligned), 0);
}

TEST(ArrayTest, KnownLengthInputCanConstructImmovableElementsDirectly) {
    std::array input{1, 2, 3};
    Array<Immovable> values(std::from_range, input);
    EXPECT_EQ(values[2].value, 3);
    auto* storage = values.data();
    auto moved = std::move(values);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(moved.data(), storage);
}

TEST(ArrayTest, RecursiveIncompleteElementTypesHaveNormalLifetimes) {
    RecursiveOwner owner;
    owner.elements = Array<RecursiveElement>(2);
    owner.elements[0].child = std::make_unique<RecursiveOwner>();
    EXPECT_EQ(owner.elements.size(), 2);
    EXPECT_TRUE(owner.elements[0].child->elements.empty());
}

TEST(ArrayTest, AlgorithmsUseTheContiguousRangeInterface) {
    Array values{3, 1, 2};
    std::ranges::sort(values);
    EXPECT_TRUE(std::ranges::equal(values, Array{1, 2, 3}));
    Array other{4};
    auto* storage = values.data();
    swap(values, other);
    EXPECT_EQ(values.size(), 1);
    EXPECT_EQ(values.front(), 4);
    EXPECT_EQ(other.data(), storage);
}

}  // namespace
