#include "pch.hpp"
#include "gtest/gtest.h"

#include "builder.hpp"
#include "diagnosis.hpp"
#include "symbol_collect.hpp"
#include "type_check.hpp"

class TemplateInstantiationTest : public ::testing::Test {
protected:
    Scope std_scope_;
    Scope* root_ = nullptr;
    CodeGenEnvironment codegen_env_;
    std::unique_ptr<Sema> sema_;

protected:
    void SetUp() override {
        TypeRegistry::instance.emplace();
        root_ = &Scope::root(std_scope_);
        sema_ = std::make_unique<Sema>(std_scope_, *root_, codegen_env_);
    }

    void TearDown() override {
        sema_.reset();
        TypeRegistry::instance.reset();
    }

    auto make_single_param_class_template(strview identifier, bool is_nttp) -> TemplateFamily* {
        std::span<ASTTemplateParameter> params = GlobalMemory::alloc_array<ASTTemplateParameter>(1);
        params[0] = ASTTemplateParameter{
            Location{},
            is_nttp,
            false,
            is_nttp ? "N"sv : "T"sv,
            std::monostate{},
            std::monostate{},
        };

        auto* class_def = new ASTClassDefinition{
            Location{},
            identifier,
            std::span<ASTExprVariant>{},
            std::span<ASTNodeVariant>{},
            std::span<ASTNodeVariant>{},
        };
        auto* primary = new ASTTemplateDefinition{Location{}, identifier, params, class_def};

        auto* family = new TemplateFamily{};
        family->decl_scope = root_;
        family->pattern_scope = nullptr;
        family->primary = primary;
        return family;
    }

    auto instantiate_class(TemplateFamily& family, std::span<const Object*> args)
        -> const InstanceType* {
        Symbol symbol = sema_->template_handler_->instantiate(family, args);
        const Type* type = Sema::get_default<SymbolKind::Type>(symbol);
        if (!type) {
            return nullptr;
        }
        return type->dyn_cast<InstanceType>();
    }
};

TEST_F(TemplateInstantiationTest, SameTypeArgumentsHitInstantiationCache) {
    TemplateFamily* family = make_single_param_class_template("Box", false);
    std::array<const Object*, 1> args = {&IntegerType::i32_instance};

    const InstanceType* first = instantiate_class(*family, args);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->primary_template_, family->primary);
    ASSERT_EQ(first->template_args_.size(), 1u);
    EXPECT_EQ(first->template_args_[0], args[0]);
    EXPECT_EQ(codegen_env_.instantiations_.size(), 1u);

    const InstanceType* second = instantiate_class(*family, args);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, first);
    EXPECT_EQ(codegen_env_.instantiations_.size(), 1u);
}

TEST_F(TemplateInstantiationTest, EquivalentNttpValuesShareOneInstantiation) {
    TemplateFamily* family = make_single_param_class_template("ConstBox", true);
    std::array<const Object*, 1> args1 = {
        static_cast<const Object*>(new IntegerValue(&IntegerType::u64_instance, std::uint64_t{7}))
    };
    std::array<const Object*, 1> args2 = {
        static_cast<const Object*>(new IntegerValue(&IntegerType::u64_instance, std::uint64_t{7}))
    };

    const InstanceType* first = instantiate_class(*family, args1);
    ASSERT_NE(first, nullptr);
    const InstanceType* second = instantiate_class(*family, args2);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first, second);
    EXPECT_EQ(codegen_env_.instantiations_.size(), 1u);
}

TEST_F(TemplateInstantiationTest, PatternArgumentReturnsPatternInstanceWithoutMaterialization) {
    TemplateFamily* family = make_single_param_class_template("PatternBox", false);
    const AutoObject* auto_object = TypeRegistry::get_auto_instances(1)[0];
    const Object* pattern_arg = auto_object->as_object(false);

    const void* input_data_ptr = nullptr;
    const InstanceType* result = nullptr;
    {
        std::array<const Object*, 1> args = {pattern_arg};
        input_data_ptr = static_cast<const void*>(args.data());
        result = instantiate_class(*family, args);
    }

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->is_pattern());
    EXPECT_EQ(result->primary_template_, family->primary);
    ASSERT_EQ(result->template_args_.size(), 1u);
    EXPECT_EQ(result->template_args_[0], pattern_arg);
    EXPECT_NE(static_cast<const void*>(result->template_args_.data()), input_data_ptr);
    EXPECT_TRUE(codegen_env_.instantiations_.empty());
}

TEST_F(TemplateInstantiationTest, TypeTemplateRejectsNonTypeArgument) {
    TemplateFamily* family = make_single_param_class_template("KindChecked", false);
    std::array<const Object*, 1> args = {
        static_cast<const Object*>(new IntegerValue(&IntegerType::i32_instance, std::int64_t{42}))
    };

    Symbol result = sema_->template_handler_->instantiate(*family, args);
    EXPECT_TRUE(holds_monostate(result));
    EXPECT_TRUE(codegen_env_.instantiations_.empty());
}

namespace {

class TemplateSpecializationResolutionTest : public ::testing::Test {
protected:
    SourceManager sources_;
    Scope std_scope_;
    CodeGenEnvironment codegen_env_;
    std::unique_ptr<Sema> sema_;
    const ASTRoot* root_ = nullptr;
    std::filesystem::path temp_file_;

protected:
    void SetUp() override {
        TypeRegistry::instance.emplace();
        Diagnostic::instance.emplace();
    }

    void TearDown() override {
        sema_.reset();

        std::error_code ec;
        if (!temp_file_.empty()) {
            std::filesystem::remove(temp_file_, ec);
        }

        GlobalMemory::monotonic()->release();
        GlobalMemory::local_pool()->release();
        Diagnostic::instance.reset();
        TypeRegistry::instance.reset();
    }

    auto analyze(strview source_text) -> bool {
        temp_file_ = write_temp_source(source_text);

        const std::uint32_t std_file_id = sources_.load_std();
        const ASTRoot* std_root = ASTBuilder(sources_, std_file_id)();
        if (std_root == nullptr) {
            return false;
        }

        std_root->scope = &std_scope_;
        std_scope_.scope_id_ = "std";
        std_scope_.is_extern_ = true;
        SymbolCollector{nullptr, nullptr}(std_root);

        std::string source_path = temp_file_.string();
        const std::uint32_t file_id = sources_.load(source_path);
        if (file_id == std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }

        root_ = ASTBuilder(sources_, file_id)();
        if (root_ == nullptr) {
            return false;
        }

        root_->scope = &Scope::root(std_scope_);
        SymbolCollector{&std_scope_, nullptr}(root_);
        if (Diagnostic::flush(sources_)) {
            return false;
        }

        sema_ = std::make_unique<Sema>(std_scope_, *root_->scope, codegen_env_);
        TypeCheckVisitor{*sema_}(root_);
        return !Diagnostic::flush(sources_);
    }

    auto find_calls(strview identifier) const
        -> std::vector<const CodeGenEnvironment::FunctionCall*> {
        std::vector<const CodeGenEnvironment::FunctionCall*> calls;
        for (const auto& [scope, table] : codegen_env_.scope_map_) {
            (void)scope;
            for (const auto& [node, value] : table) {
                (void)node;
                if (const auto* call = std::get_if<CodeGenEnvironment::FunctionCall*>(&value)) {
                    if ((*call)->identifier == identifier) {
                        calls.push_back(*call);
                    }
                }
            }
        }
        return calls;
    }

    auto expect_single_call_return(strview identifier, const Type* return_type) const -> void {
        auto calls = find_calls(identifier);
        ASSERT_EQ(calls.size(), 1u);
        ASSERT_NE(calls.front(), nullptr);
        ASSERT_NE(calls.front()->func_type, nullptr);
        EXPECT_EQ(calls.front()->func_type->return_type_, return_type);
    }

private:
    auto write_temp_source(strview source_text) const -> std::filesystem::path {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "zinc_template_resolution_tests";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        static unsigned long long serial = 0;
        ++serial;
        const auto addr = static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this));
        const std::filesystem::path file =
            dir / ("case_" + std::to_string(addr) + "_" + std::to_string(serial) + ".zn");

        std::ofstream out(file, std::ios::binary);
        out << source_text;
        out.flush();
        return file;
    }
};

TEST_F(TemplateSpecializationResolutionTest, FallsBackToPrimaryWhenNoSpecializationMatches) {
    ASSERT_TRUE(analyze(R"(
class Pick<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<X: type> class Pick<X, f64> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

fn probe() {
    Pick::<bool, i32>.marker();
}
)"));

    expect_single_call_return("marker", &IntegerType::i32_instance);
}

TEST_F(
    TemplateSpecializationResolutionTest, MatchingPartialSpecializationOverridesPrimaryTemplate
) {
    ASSERT_TRUE(analyze(R"(
class Pick<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<X: type> class Pick<X, f64> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

fn probe() {
    Pick::<bool, f64>.marker();
}
)"));

    expect_single_call_return("marker", &IntegerType::i64_instance);
}

TEST_F(TemplateSpecializationResolutionTest, FullSpecializationBeatsPartialSpecialization) {
    ASSERT_TRUE(analyze(R"(
class Pick<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<X: type> class Pick<X, f64> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

specialize class Pick<i32, f64> {
    static fn marker() -> bool {
        return true;
    }
}

fn probe() {
    Pick::<i32, f64>.marker();
}
)"));

    expect_single_call_return("marker", &BooleanType::instance);
}

TEST_F(TemplateSpecializationResolutionTest, MoreSpecializedPartialSpecializationIsPreferred) {
    ASSERT_TRUE(analyze(R"(
class Pick<A: type, B: type, C: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<X: type, Y: type> class Pick<X, Y, f64> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

specialize<Z: type> class Pick<Z, f64, f64> {
    static fn marker() -> bool {
        return true;
    }
}

fn probe() {
    Pick::<i32, f64, f64>.marker();
}
)"));

    expect_single_call_return("marker", &BooleanType::instance);
}

TEST_F(TemplateSpecializationResolutionTest, RepeatedTypeBindingPatternMatchesConsistentArguments) {
    ASSERT_TRUE(analyze(R"(
class Pair<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<T: type> class Pair<T, T> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

fn probe() {
    Pair::<i32, i32>.marker();
}
)"));

    expect_single_call_return("marker", &IntegerType::i64_instance);
}

TEST_F(TemplateSpecializationResolutionTest, RepeatedTypeBindingFallsBackWhenArgumentsDiffer) {
    ASSERT_TRUE(analyze(R"(
class Pair<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<T: type> class Pair<T, T> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

fn probe() {
    Pair::<i32, bool>.marker();
}
)"));

    expect_single_call_return("marker", &IntegerType::i32_instance);
}

TEST_F(TemplateSpecializationResolutionTest, NestedTemplatePatternCanBeMatched) {
    ASSERT_TRUE(analyze(R"(
class Wrap<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<T: type> class Wrap<std.vector<T>, T> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

fn probe() {
    Wrap::<std.vector<i32>, i32>.marker();
}
)"));

    expect_single_call_return("marker", &IntegerType::i64_instance);
}

TEST_F(TemplateSpecializationResolutionTest, CrossPartialSpecializationsAreAmbiguous) {
    EXPECT_FALSE(analyze(R"(
class Ambiguous<A: type, B: type> {
    static fn marker() -> i32 {
        return 0i32;
    }
}

specialize<X: type> class Ambiguous<X, f64> {
    static fn marker() -> i64 {
        return 1i64;
    }
}

specialize<Y: type> class Ambiguous<i64, Y> {
    static fn marker() -> bool {
        return true;
    }
}

fn probe() {
    Ambiguous::<i64, f64>.marker();
}
)"));
}

TEST_F(TemplateSpecializationResolutionTest, SpecializationWithoutPatternIsRejected) {
    EXPECT_FALSE(analyze(R"(
class Bad<T: type> {
}

specialize class Bad {
}
)"));
}

TEST_F(TemplateSpecializationResolutionTest, TemplateAndSpecializationListsCannotAppearTogether) {
    EXPECT_FALSE(analyze(R"(
specialize<X: type> class Bad<Y: type> {
}
)"));
}

TEST_F(TemplateSpecializationResolutionTest, SpecializationBeforePrimaryTemplateIsRejected) {
    EXPECT_FALSE(analyze(R"(
specialize class Late<i32> {
}

class Late<T: type> {
}
)"));
}

}  // namespace
