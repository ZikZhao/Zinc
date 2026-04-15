#include "pch.hpp"
#include "gtest/gtest.h"

#include "builder.hpp"
#include "diagnosis.hpp"
#include "symbol_collect.hpp"
#include "type_check.hpp"

namespace {

class OverloadResolutionTest : public ::testing::Test {
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

    auto expect_single_call_param(
        strview identifier, std::size_t index, const Type* param_type
    ) const -> void {
        auto calls = find_calls(identifier);
        ASSERT_EQ(calls.size(), 1u);
        ASSERT_NE(calls.front(), nullptr);
        ASSERT_NE(calls.front()->func_type, nullptr);
        ASSERT_GT(calls.front()->func_type->parameters_.size(), index);
        EXPECT_EQ(calls.front()->func_type->parameters_[index], param_type);
    }

private:
    auto write_temp_source(strview source_text) const -> std::filesystem::path {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "zinc_overload_resolution_tests";
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

TEST_F(OverloadResolutionTest, MutableReferenceIsPreferredOverConstReference) {
    ASSERT_TRUE(analyze(R"(
fn choose(x: &i32) -> i32 {
    return 1i32;
}

fn choose(x: &mut i32) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    choose(v);
}
)"));

    auto calls = find_calls("choose");
    ASSERT_EQ(calls.size(), 1u);

    const FunctionType* selected = calls.front()->func_type;
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->parameters_.size(), 1u);
    EXPECT_EQ(
        selected->parameters_[0],
        TypeRegistry::get<ReferenceType>(&IntegerType::i32_instance, true, false)
    );
    EXPECT_EQ(selected->return_type_, &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, RvaluePrefersByValueOverReferenceBinding) {
    ASSERT_TRUE(analyze(R"(
fn pick(x: i32) -> i32 {
    return 1i32;
}

fn pick(x: &i32) -> i64 {
    return 2i64;
}

fn probe() {
    pick(42i32);
}
)"));

    auto calls = find_calls("pick");
    ASSERT_EQ(calls.size(), 1u);

    const FunctionType* selected = calls.front()->func_type;
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->parameters_.size(), 1u);
    EXPECT_EQ(selected->parameters_[0], &IntegerType::i32_instance);
    EXPECT_EQ(selected->return_type_, &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, ExactPointerMatchBeatsVoidPointerUpcast) {
    ASSERT_TRUE(analyze(R"(
fn ptr_pick(x: *i32) -> i32 {
    return 1i32;
}

fn ptr_pick(x: *void) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    ptr_pick(&v);
}
)"));

    auto calls = find_calls("ptr_pick");
    ASSERT_EQ(calls.size(), 1u);

    const FunctionType* selected = calls.front()->func_type;
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->parameters_.size(), 1u);
    EXPECT_EQ(
        selected->parameters_[0], TypeRegistry::get<PointerType>(&IntegerType::i32_instance, false)
    );
    EXPECT_EQ(selected->return_type_, &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, NonTemplateOverloadWinsWhenEquallyViable) {
    ASSERT_TRUE(analyze(R"(
fn sel<T: type>(x: T) -> i64 {
    return 1i64;
}

fn sel(x: i32) -> i32 {
    return 2i32;
}

fn probe() {
    sel(7i32);
}
)"));

    auto calls = find_calls("sel");
    ASSERT_EQ(calls.size(), 1u);

    const FunctionType* selected = calls.front()->func_type;
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->parameters_.size(), 1u);
    EXPECT_EQ(selected->parameters_[0], &IntegerType::i32_instance);
    EXPECT_EQ(selected->return_type_, &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, ImmutableReferenceIsChosenForImmutableLvalue) {
    ASSERT_TRUE(analyze(R"(
fn imm_ref_pick(x: &i32) -> i32 {
    return 1i32;
}

fn imm_ref_pick(x: &mut i32) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    imm_ref_pick(v);
}
)"));

    expect_single_call_return("imm_ref_pick", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, LvaluePrefersReferenceOverByValue) {
    ASSERT_TRUE(analyze(R"(
fn lvalue_pick(x: i32) -> i32 {
    return 1i32;
}

fn lvalue_pick(x: &i32) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    lvalue_pick(v);
}
)"));

    expect_single_call_return("lvalue_pick", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, XvaluePrefersMoveReferenceOverOtherReferences) {
    ASSERT_TRUE(analyze(R"(
fn move_pick(x: move &i32) -> i64 {
    return 1i64;
}

fn move_pick(x: &mut i32) -> i32 {
    return 2i32;
}

fn move_pick(x: &i32) -> bool {
    return false;
}

fn probe() {
    let mut v: i32 = 0i32;
    move_pick(move v);
}
)"));

    expect_single_call_return("move_pick", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, XvaluePrefersMoveReferenceOverByValue) {
    ASSERT_TRUE(analyze(R"(
fn move_vs_value(x: i32) -> i32 {
    return 1i32;
}

fn move_vs_value(x: move &i32) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    move_vs_value(move v);
}
)"));

    expect_single_call_return("move_vs_value", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, LvalueDoesNotBindToMoveReference) {
    ASSERT_TRUE(analyze(R"(
fn move_bind(x: move &i32) -> i64 {
    return 1i64;
}

fn move_bind(x: &i32) -> i32 {
    return 2i32;
}

fn probe() {
    let mut v: i32 = 0i32;
    move_bind(v);
}
)"));

    expect_single_call_return("move_bind", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, VoidPointerOverloadIsUsedAsFallback) {
    ASSERT_TRUE(analyze(R"(
fn ptr_fallback(x: *void) -> i64 {
    return 1i64;
}

fn ptr_fallback(x: *i32) -> i32 {
    return 2i32;
}

fn probe() {
    let mut b: bool = true;
    ptr_fallback(&b);
}
)"));

    expect_single_call_return("ptr_fallback", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, NullptrPrefersPointerOverScalar) {
    ASSERT_TRUE(analyze(R"(
fn nullptr_pick(x: *i32) -> i64 {
    return 1i64;
}

fn nullptr_pick(x: i32) -> i32 {
    return 2i32;
}

fn probe() {
    nullptr_pick(nullptr);
}
)"));

    expect_single_call_return("nullptr_pick", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, NullptrBetweenPointerTargetsIsAmbiguous) {
    EXPECT_FALSE(analyze(R"(
fn nullptr_amb(x: *i32) -> i32 {
    return 1i32;
}

fn nullptr_amb(x: *bool) -> i64 {
    return 2i64;
}

fn probe() {
    nullptr_amb(nullptr);
}
)"));
}

TEST_F(OverloadResolutionTest, MutableAndConstPointerOverloadsCanBeAmbiguous) {
    EXPECT_FALSE(analyze(R"(
fn ptr_mut_amb(x: *i32) -> i32 {
    return 1i32;
}

fn ptr_mut_amb(x: *mut i32) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    ptr_mut_amb(&v);
}
)"));
}

TEST_F(OverloadResolutionTest, ImmutablePointerCannotBindToMutablePointer) {
    EXPECT_FALSE(analyze(R"(
fn ptr_need_mut(x: *mut i32) -> i32 {
    return 1i32;
}

fn probe() {
    let v: i32 = 0i32;
    ptr_need_mut(&v);
}
)"));
}

TEST_F(OverloadResolutionTest, TemplateIsUsedWhenNormalOverloadIsNotViable) {
    ASSERT_TRUE(analyze(R"(
fn tmpl_fallback(x: bool) -> i32 {
    return 1i32;
}

fn tmpl_fallback<T: type>(x: T) -> i64 {
    return 2i64;
}

fn probe() {
    tmpl_fallback(1i32);
}
)"));

    expect_single_call_return("tmpl_fallback", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, TemplateCanBeatNormalWhenItIsStrictlyBetter) {
    ASSERT_TRUE(analyze(R"(
fn tmpl_better(x: &i32) -> i32 {
    return 1i32;
}

fn tmpl_better<T: type>(x: &mut T) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    tmpl_better(v);
}
)"));

    expect_single_call_return("tmpl_better", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, TemplateDeductionFailureFallsBackToNormalOverload) {
    ASSERT_TRUE(analyze(R"(
fn tmpl_deduce_fail<T: type>(x: T, y: T) -> i64 {
    return 1i64;
}

fn tmpl_deduce_fail(x: i32, y: bool) -> i32 {
    return 2i32;
}

fn probe() {
    tmpl_deduce_fail(1i32, true);
}
)"));

    expect_single_call_return("tmpl_deduce_fail", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, TemplateHandlesHomogeneousArgumentsWhenNormalDoesNotMatch) {
    ASSERT_TRUE(analyze(R"(
fn tmpl_homogeneous<T: type>(x: T, y: T) -> i64 {
    return 1i64;
}

fn tmpl_homogeneous(x: i32, y: bool) -> i32 {
    return 2i32;
}

fn probe() {
    tmpl_homogeneous(1i32, 2i32);
}
)"));

    expect_single_call_return("tmpl_homogeneous", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, NoViableOverloadForImmutableToMutableReference) {
    EXPECT_FALSE(analyze(R"(
fn no_viable_ref(x: &mut i32) -> i32 {
    return 1i32;
}

fn probe() {
    let v: i32 = 0i32;
    no_viable_ref(v);
}
)"));
}

TEST_F(OverloadResolutionTest, NoViableOverloadWhenArityDoesNotMatchAnyCandidate) {
    EXPECT_FALSE(analyze(R"(
fn arity_fail(x: i32) -> i32 {
    return 1i32;
}

fn probe() {
    arity_fail();
}
)"));
}

TEST_F(OverloadResolutionTest, OneArgumentCallChoosesSingleParameterOverload) {
    ASSERT_TRUE(analyze(R"(
fn arity_pick(x: i32) -> i32 {
    return 1i32;
}

fn arity_pick(x: i32, y: i32) -> i64 {
    return 2i64;
}

fn probe() {
    arity_pick(1i32);
}
)"));

    expect_single_call_return("arity_pick", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, TwoArgumentCallChoosesTwoParameterOverload) {
    ASSERT_TRUE(analyze(R"(
fn arity_pick2(x: i32) -> i32 {
    return 1i32;
}

fn arity_pick2(x: i32, y: i32) -> i64 {
    return 2i64;
}

fn probe() {
    arity_pick2(1i32, 2i32);
}
)"));

    expect_single_call_return("arity_pick2", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, CrossBetterCandidatesAreAmbiguous) {
    EXPECT_FALSE(analyze(R"(
fn cross_amb(x: &i32, y: i32) -> i32 {
    return 1i32;
}

fn cross_amb(x: i32, y: &i32) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    cross_amb(v, v);
}
)"));
}

TEST_F(OverloadResolutionTest, BetterCandidateAcrossAllArgumentsWins) {
    ASSERT_TRUE(analyze(R"(
fn multi_best(x: &i32, y: &i32) -> i32 {
    return 1i32;
}

fn multi_best(x: i32, y: i32) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    multi_best(v, v);
}
)"));

    expect_single_call_return("multi_best", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, EquivalentTemplatesAreAmbiguous) {
    EXPECT_FALSE(analyze(R"(
fn tpl_amb<T: type>(x: &T) -> i32 {
    return 1i32;
}

fn tpl_amb<U: type>(x: &U) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    tpl_amb(v);
}
)"));
}

TEST_F(OverloadResolutionTest, MoreSpecializedTemplateBeatsGenericTemplateForLvalue) {
    ASSERT_TRUE(analyze(R"(
fn tpl_spec<T: type>(x: T) -> i32 {
    return 1i32;
}

fn tpl_spec<T: type>(x: &T) -> i64 {
    return 2i64;
}

fn probe() {
    let v: i32 = 0i32;
    tpl_spec(v);
}
)"));

    expect_single_call_return("tpl_spec", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, ValueTemplateBeatsReferenceTemplateForRvalue) {
    ASSERT_TRUE(analyze(R"(
fn tpl_rv<T: type>(x: &T) -> i32 {
    return 1i32;
}

fn tpl_rv<T: type>(x: T) -> i64 {
    return 2i64;
}

fn probe() {
    tpl_rv(1i32);
}
)"));

    expect_single_call_return("tpl_rv", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, NullptrBetweenTypedAndVoidPointersIsAmbiguous) {
    EXPECT_FALSE(analyze(R"(
fn null_void(x: *i32) -> i32 {
    return 1i32;
}

fn null_void(x: *void) -> i64 {
    return 2i64;
}

fn probe() {
    null_void(nullptr);
}
)"));
}

TEST_F(OverloadResolutionTest, NonTemplateStillWinsWhenTemplateIsEquivalentForPointers) {
    ASSERT_TRUE(analyze(R"(
fn ptr_nt(x: *i32) -> i32 {
    return 1i32;
}

fn ptr_nt<T: type>(x: T) -> i64 {
    return 2i64;
}

fn probe() {
    let mut v: i32 = 0i32;
    ptr_nt(&v);
}
)"));

    expect_single_call_return("ptr_nt", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, MethodOverloadPrefersMutableReceiverOnMutableObject) {
    ASSERT_TRUE(analyze(R"(
class ReceiverMut {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }

    fn tag(self: &Self) -> i32 {
        return 1i32;
    }

    fn tag(self: &mut Self) -> i64 {
        return 2i64;
    }
}

fn probe() {
    let mut r: ReceiverMut = ReceiverMut(1i32);
    r.tag();
}
)"));

    expect_single_call_return("tag", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, MethodOverloadPrefersConstReceiverOnImmutableObject) {
    ASSERT_TRUE(analyze(R"(
class ReceiverConst {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }

    fn tag(self: &Self) -> i32 {
        return 1i32;
    }

    fn tag(self: &mut Self) -> i64 {
        return 2i64;
    }
}

fn probe() {
    let r: ReceiverConst = ReceiverConst(1i32);
    r.tag();
}
)"));

    expect_single_call_return("tag", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, MethodCallFailsWhenOnlyMutableReceiverExistsForImmutableObject) {
    EXPECT_FALSE(analyze(R"(
class ReceiverOnlyMut {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }

    fn only_mut(self: &mut Self) -> i32 {
        return 1i32;
    }
}

fn probe() {
    let r: ReceiverOnlyMut = ReceiverOnlyMut(1i32);
    r.only_mut();
}
)"));
}

TEST_F(OverloadResolutionTest, ConstructorLvaluePrefersReferenceOverload) {
    ASSERT_TRUE(analyze(R"(
class CtorRef {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }

    init(v: &i32) {
        return Self{x: v};
    }
}

fn probe() {
    let v: i32 = 7i32;
    let a: CtorRef = CtorRef(v);
}
)"));

    expect_single_call_param(
        constructor_symbol,
        0,
        TypeRegistry::get<ReferenceType>(&IntegerType::i32_instance, false, false)
    );
}

TEST_F(OverloadResolutionTest, ConstructorRvaluePrefersByValueOverload) {
    ASSERT_TRUE(analyze(R"(
class CtorValue {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }

    init(v: &i32) {
        return Self{x: v};
    }
}

fn probe() {
    let a: CtorValue = CtorValue(7i32);
}
)"));

    expect_single_call_param(constructor_symbol, 0, &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, ConstructorMutableLvaluePrefersMutableReferenceOverload) {
    ASSERT_TRUE(analyze(R"(
class CtorMutRef {
    let x: i32;

    init(v: &i32) {
        return Self{x: v};
    }

    init(v: &mut i32) {
        return Self{x: v};
    }
}

fn probe() {
    let mut v: i32 = 7i32;
    let a: CtorMutRef = CtorMutRef(v);
}
)"));

    expect_single_call_param(
        constructor_symbol,
        0,
        TypeRegistry::get<ReferenceType>(&IntegerType::i32_instance, true, false)
    );
}

TEST_F(OverloadResolutionTest, MethodCrossBetterCandidatesAreAmbiguous) {
    EXPECT_FALSE(analyze(R"(
class MethodCrossAmb {
    init() {
        return Self{};
    }

    fn f(self: &Self, x: &i32, y: i32) -> i32 {
        return 1i32;
    }

    fn f(self: &Self, x: i32, y: &i32) -> i64 {
        return 2i64;
    }
}

fn probe() {
    let o: MethodCrossAmb = MethodCrossAmb();
    let v: i32 = 0i32;
    o.f(v, v);
}
)"));
}

TEST_F(OverloadResolutionTest, MethodNonTemplatePreferredWhenEquivalentTemplateExists) {
    ASSERT_TRUE(analyze(R"(
class MethodNtPref {
    init() {
        return Self{};
    }

    fn g(self: &Self, x: i32) -> i32 {
        return 1i32;
    }

    fn g<T: type>(self: &Self, x: T) -> i64 {
        return 2i64;
    }
}

fn probe() {
    let o: MethodNtPref = MethodNtPref();
    o.g(1i32);
}
)"));

    expect_single_call_return("g", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, MethodTemplateUsedWhenNonTemplateIsNotViable) {
    ASSERT_TRUE(analyze(R"(
class MethodTplFallback {
    init() {
        return Self{};
    }

    fn h(self: &Self, x: bool) -> i32 {
        return 1i32;
    }

    fn h<T: type>(self: &Self, x: T) -> i64 {
        return 2i64;
    }
}

fn probe() {
    let o: MethodTplFallback = MethodTplFallback();
    o.h(1i32);
}
)"));

    expect_single_call_return("h", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, TemporaryExtendsWhenBindingScalarToImmutableReferenceVariable) {
    ASSERT_TRUE(analyze(R"(
fn probe() {
    let r: &i32 = 42i32;
}
)"));
}

TEST_F(OverloadResolutionTest, TemporaryCannotBindToMutableReferenceVariable) {
    EXPECT_FALSE(analyze(R"(
fn probe() {
    let r: &mut i32 = 42i32;
}
)"));
}

TEST_F(OverloadResolutionTest, PureRvalueCanBindToMoveReferenceVariable) {
    ASSERT_TRUE(analyze(R"(
fn probe() {
    let r: move &i32 = 42i32;
}
)"));
}

TEST_F(OverloadResolutionTest, PureRvalueCanBePassedToMoveReferenceParameter) {
    ASSERT_TRUE(analyze(R"(
fn take_move(x: move &i32) -> i64 {
    return 1i64;
}

fn probe() {
    take_move(42i32);
}
)"));

    expect_single_call_return("take_move", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, PureRvaluePrefersByValueOverMoveReferenceParameter) {
    ASSERT_TRUE(analyze(R"(
fn rv_pref(x: move &i32) -> i64 {
    return 1i64;
}

fn rv_pref(x: i32) -> i32 {
    return 2i32;
}

fn probe() {
    rv_pref(42i32);
}
)"));

    expect_single_call_return("rv_pref", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, TemporaryExtendsWhenBindingClassTemporaryToImmutableReference) {
    ASSERT_TRUE(analyze(R"(
class LifeBox {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }
}

fn probe() {
    let r: &LifeBox = LifeBox(1i32);
}
)"));
}

TEST_F(OverloadResolutionTest, ClassTemporaryCannotBindToMutableReferenceVariable) {
    EXPECT_FALSE(analyze(R"(
class LifeBoxMut {
    let x: i32;

    init(v: i32) {
        return Self{x: v};
    }
}

fn probe() {
    let r: &mut LifeBoxMut = LifeBoxMut(1i32);
}
)"));
}

TEST_F(OverloadResolutionTest, ExtendedReferenceParticipatesAsLvalueInOverloadResolution) {
    ASSERT_TRUE(analyze(R"(
fn ext_pick(x: &i32) -> i64 {
    return 1i64;
}

fn ext_pick(x: i32) -> i32 {
    return 2i32;
}

fn probe() {
    let r: &i32 = 42i32;
    ext_pick(r);
}
)"));

    expect_single_call_return("ext_pick", &IntegerType::i64_instance);
}

TEST_F(OverloadResolutionTest, ExtendedReferenceCanBeForwardedToReferenceParameter) {
    ASSERT_TRUE(analyze(R"(
fn sink_ref(x: &i32) -> i32 {
    return x;
}

fn probe() {
    let r: &i32 = 42i32;
    sink_ref(r);
}
)"));

    expect_single_call_return("sink_ref", &IntegerType::i32_instance);
}

TEST_F(OverloadResolutionTest, ExtendedReferenceCannotBePassedToMutableReferenceParameter) {
    EXPECT_FALSE(analyze(R"(
fn sink_mut(x: &mut i32) -> i32 {
    return x;
}

fn probe() {
    let r: &i32 = 42i32;
    sink_mut(r);
}
)"));
}

}  // namespace
