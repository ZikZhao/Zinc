#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

int main(int argc, char* argv[]) {
    SourceManager sources;
    ImportManager<ASTRoot> importer(sources);

    std::string_view input_path = (argc > 1) ? argv[1] : "<stdin>";
    ASTBuilder builder(*sources.load(input_path), importer);
    std::unique_ptr<ASTRoot> root = builder();

    Scope ctx;
    TypeRegistry types;
    OpDispatcher ops(types);
    root->collect_symbols(ctx, ops);
    TypeChecker checker(ctx, ops, types);
    root->check_types(checker);

    bool has_error = Diagnostic::print(sources);

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
