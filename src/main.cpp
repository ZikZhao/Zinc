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

auto get_root(SourceManager& sources, ImportManager<ASTRoot>& importer) -> Scope& {
    static Scope& std_scope = [&]() -> Scope& {
        ASTBuilder builder(*sources.load_std(), importer);
        const ASTRoot* std_root = builder();
        static Scope scope;
        scope.scope_id_ = "std";
        scope.is_extern_ = true;
        SymbolCollector{scope}(std_root);
        for (const auto& [identifier, meta_fn] : Meta::get_metas()) {
            scope.add_meta(identifier, meta_fn);
        }
        return scope;
    }();
    return Scope::root(std_scope);
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

    Scope& scope = get_root(sources, importer);
    SymbolCollector{scope}(root);

    CodeGenEnvironment codegen_env;
    Sema sema{scope, codegen_env};
    TypeCheckVisitor{sema}(root);

    bool has_error = Diagnostic::print(sources);
    if (has_error) return EXIT_FAILURE;

    return codegen(sources, sema, codegen_env);
}
