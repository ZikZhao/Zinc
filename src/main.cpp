#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "codegen.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"
#include "symbol_collect.hpp"
#include "type_check.hpp"

class ThreadGuard {
public:
    ThreadGuard() {
        TypeRegistry::instance.emplace();
        Diagnostic::instance.emplace();
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard(ThreadGuard&&) = delete;
    auto operator=(const ThreadGuard&) -> ThreadGuard& = delete;
    auto operator=(ThreadGuard&&) -> ThreadGuard& = delete;
    ~ThreadGuard() {
        GlobalMemory::monotonic()->release();
        GlobalMemory::pool()->release();
        TypeRegistry::instance.reset();
        Diagnostic::instance.reset();
    }
};

auto get_root(SourceManager& sources, ImportManager<ASTRoot>& importer, const ASTRoot* root)
    -> std::pair<Scope&, MemberAccessHandler> {
    static auto [std_scope, std_sema] = [&]() {
        ASTBuilder builder(*sources.load_std(), importer);
        ASTRoot* std_root = builder();
        static Scope scope;
        MemberAccessHandler sema;
        SymbolCollector{scope, sema}(std_root);
        return std::pair{&scope, sema};
    }();
    return {Scope::root(*std_scope, root), std_sema};
}

auto main(int argc, char* argv[]) -> int {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.zn>\n";
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
    SymbolCollector symbol_collector(scope, sema);
    symbol_collector(root);

    TypeChecker checker(scope, sema);
    TypeCheckVisitor{checker}(root);

    bool has_error = Diagnostic::print(sources);
    if (has_error) return EXIT_FAILURE;

    return codegen(sources);
}
