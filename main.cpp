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
        const std::string_view name_;
        const TypeRef type_;
        const std::function<ValueRef (const Arguments&)> func_;
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
    std::pair<ScopeDefinition, Context> GetBuiltins() {
        const std::vector<BuiltinFunction*> builtins = {
            &Print,
        };
        const ScopeDefinition scope = builtins | std::views::transform([](BuiltinFunction* builtin) {
            return std::pair(std::string(builtin->name_), builtin->type_);
        }) | std::ranges::to<std::vector<std::pair<std::string, TypeRef>>>();
        const Context ctx = builtins | std::views::transform([](BuiltinFunction* builtin) {
            return ValueRef(new FunctionValue(builtin->func_, builtin->type_));
        }) | std::ranges::to<Context>();
        return { std::move(scope), std::move(ctx) };
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

    auto [defs, globals] = Builtins::GetBuiltins();
    root->first_analyze(&defs);
    root->second_analyze(&defs);
    root->execute(globals, globals);
    delete root;
}