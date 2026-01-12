#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"
#include "transpiler.hpp"

class ThreadGuard {
public:
    ~ThreadGuard() {
        TypeRegistry::instance = TypeRegistry();
        Diagnostic::instance = Diagnostic();
    }
};

int main(int argc, char* argv[]) {
    ThreadGuard guard;

    SourceManager sources;
    ImportManager<ASTRoot> importer(sources);

    std::string_view input_path = (argc > 1) ? argv[1] : "<stdin>";
    ASTBuilder builder(*sources.load(input_path), importer);
    std::unique_ptr<ASTRoot> root = builder();

    Scope scope;
    OperationHandler ops;
    root->collect_symbols(scope, ops);
    TypeChecker checker(scope, ops);
    root->check_types(checker);

    bool has_error = Diagnostic::print(sources);
    if (!has_error) {
        return transpile(root.get(), sources, checker);
    }

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
