#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"
#include "symbol_collect.hpp"
#include "transpiler.hpp"
#include "type_check.hpp"

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

std::pair<Scope&, MemberAccessHandler> get_root(
    SourceManager& sources, ImportManager<ASTRoot>& importer, const ASTRoot* root
) {
    static auto [std_scope, std_sema] = [&]() {
        ASTBuilder builder(*sources.load_std(), importer);
        ASTRoot* root = builder();
        static Scope scope;
        MemberAccessHandler sema;
        SymbolCollectVisitor(scope, sema)(root);
        return std::pair{&scope, sema};
    }();
    return {Scope::root(*std_scope, root), std_sema};
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.zn>" << std::endl;
        return EXIT_FAILURE;
    }

    ThreadGuard guard;

    SourceManager sources;
    ImportManager<ASTRoot> importer(sources);
    ASTBuilder builder(*sources.load(argv[1]), importer);
    ASTRoot* root = builder();

    if (root == nullptr) {
        Diagnostic::error("Failed to parse input");
        return EXIT_FAILURE;
    }

    auto [scope, sema] = get_root(sources, importer, root);
    SymbolCollectVisitor(scope, sema)(root);

    DependencyGraph dep_graph;
    TypeChecker checker(scope, dep_graph, sema);
    TypeCheckVisitor type_checker(checker);
    type_checker(root);

    bool has_error = Diagnostic::print(sources);
    if (!has_error) {
        return transpile_all(root, sources, dep_graph);
    } else {
        return EXIT_FAILURE;
    }
}
