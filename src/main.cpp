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
    ThreadGuard() {
        TypeRegistry::instance.emplace();
        Diagnostic::instance.emplace();
    }
    ~ThreadGuard() {
        GlobalMemory::monotonic()->release();
        GlobalMemory::pool()->release();
        TypeRegistry::instance.reset();
        Diagnostic::instance.reset();
    }
};

int main(int argc, char* argv[]) {
    ThreadGuard guard;

    SourceManager sources;
    ImportManager<ASTRoot> importer(sources);

    std::string_view input_path = (argc > 1) ? argv[1] : "<stdin>";
    ASTBuilder builder(*sources.load(input_path), importer);
    ASTRoot* root = builder();

    if (root == nullptr) {
        Diagnostic::error("Failed to parse input");
        return EXIT_FAILURE;
    }

    Scope scope;
    OperationHandler ops;
    root->collect_symbols(scope, ops);

    TypeChecker checker(scope, ops);
    root->check_types(checker);

    bool has_error = Diagnostic::print(sources);
    if (!has_error) {
        transpile(root, sources, checker);
    }

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
