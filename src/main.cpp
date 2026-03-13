#include "pch.hpp"

#include "ast.hpp"
#include "builder.hpp"
#include "codegen.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
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
        GlobalMemory::local_pool()->release();
        TypeRegistry::instance.reset();
        Diagnostic::instance.reset();
    }
};

auto get_root(SourceManager& sources, ImportManager<ASTRoot>& importer)
    -> std::pair<Scope&, OperatorRegistry> {
    static auto [std_scope, std_operators] = [&]() {
        ASTBuilder builder(*sources.load_std(), importer);
        const ASTRoot* std_root = builder();
        static Scope scope;
        OperatorRegistry operators;
        SymbolCollector{scope, operators}(std_root);
        return std::pair{&scope, operators};
    }();
    return {Scope::root(*std_scope), std_operators};
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
    const ASTRoot* root = builder();

    if (root == nullptr) {
        Diagnostic::error("Failed to parse input");
        return EXIT_FAILURE;
    }

    auto [scope, operators] = get_root(sources, importer);
    SymbolCollector{scope, operators}(root);

    Sema sema{scope, std::move(operators)};
    TypeCheckVisitor{sema}(root);

    bool has_error = Diagnostic::print(sources);
    if (has_error) return EXIT_FAILURE;

    return codegen(sources);
}
