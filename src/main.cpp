#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

int main(int argc, char* argv[]) {
    SourceManager sources;
    ImportManager importer(sources);

    std::string_view input_path = (argc > 1) ? argv[1] : "<stdin>";
    ImportFuture import_future = importer.import(input_path);
    const std::unique_ptr<ASTRoot>& root = std::move(import_future).get();

    Scope ctx;
    TypeRegistry types;
    OpDispatcher ops(types);
    root->collect_symbols(ctx, ops);
    TypeChecker checker(ctx, ops, types);
    root->check_types(checker);

    bool has_error = Diagnostic::print(sources);

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
