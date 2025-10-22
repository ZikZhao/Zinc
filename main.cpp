#include "pch.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "out/parser.tab.hpp"

std::ifstream yyin;

std::ostream& operator << (std::ostream& os, const Location& loc) {
    return os << loc.begin.line << ":" << loc.begin.column << "-" << loc.end.line << ":" << loc.end.column;
}

void yy::parser::error(const Location& location, const std::string& message) {
    std::cerr << "Parsing terminated due to errors: " << message << std::endl;
}

namespace Builtins {
    struct BuiltinFunction {
        const std::string_view name;
        const TypeRef type;
        const std::function<ValueRef (const Arguments&)> func;
    };
    BuiltinFunction Print = {
        "print",
        new FunctionType({}, TypeRef(new AnyType()), TypeRef(new NullType())),
        [](const Arguments& args) -> ValueRef {
            try {
                std::cout << args.at(0)->repr() << std::endl;
                return new NullValue();
            } catch (const std::out_of_range& e) {
                throw ArgumentException(e.what());
            }
        },
    };
    const std::vector<BuiltinFunction*> AllBuiltins = {
        &Print,
    };
    ScopeDefinition GetBuiltinsScope() {
        return ScopeDefinition(AllBuiltins | std::views::transform([](BuiltinFunction* builtin) {
            return std::pair(std::string(builtin->name), builtin->type);
        }) | std::ranges::to<std::vector<std::pair<std::string, TypeRef>>>());
    }
    ScopeStorage GetBuiltinsScopeStorage() {
        return AllBuiltins | std::views::transform([](BuiltinFunction* builtin) {
            return ValueRef(new FunctionValue(builtin->func, builtin->type));
        }) | std::ranges::to<std::vector<ValueRef>>();
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        yyin.open(argv[1]);
        if (yyin.fail()) {
            std::cerr << "Error: Cannot open file " << argv[1] << std::endl;
            return 1;
        }
    }

    ASTNode* root = nullptr;
    yy::parser parser(root);
    parser.set_debug_level(1);
    parser.parse();

    std::cout << std::endl;
    root->print(std::cout);

    ScopeDefinition builtins = Builtins::GetBuiltinsScope();
    root->first_analyze(builtins);
    root->second_analyze(builtins);
    ScopeStorage globals = Builtins::GetBuiltinsScopeStorage();
    root->execute(globals, globals);
    delete root;
}