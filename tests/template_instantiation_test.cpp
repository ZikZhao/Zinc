#include "pch.hpp"
#include "gtest/gtest.h"

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
