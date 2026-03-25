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

auto get_std_scope(SourceManager& sources) -> Scope& {
    static Scope& std_scope = [&]() -> Scope& {
        ASTBuilder builder(sources, sources.load_std());
        const ASTRoot* std_root = builder();
        static Scope scope;
        std_root->scope = &scope;
        scope.scope_id_ = "std";
        scope.is_extern_ = true;
        SymbolCollector{nullptr, nullptr}(std_root);
        for (const auto& [identifier, meta_fn] : Meta::get_metas()) {
            scope.add_meta(identifier, meta_fn);
        }
        return scope;
    }();
    return std_scope;
}

auto main(int argc, char* argv[]) -> int {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.zn>\n";
        return EXIT_FAILURE;
    }

    ThreadGuard guard;

    SourceManager sources;
    Scope& std_scope = get_std_scope(sources);
    uint32_t file_id = sources.load(argv[1]);
    if (file_id == std::numeric_limits<uint32_t>::max()) {
        Diagnostic::error_module_not_found(argv[1]);
        return EXIT_FAILURE;
    }
    const ASTRoot* root = ASTBuilder{sources, file_id}();

    if (root == nullptr) {
        Diagnostic::print_error_msg("Failed to parse input");
        return EXIT_FAILURE;
    }

    root->scope = &Scope::root(std_scope);
    SymbolCollector{&std_scope, nullptr}(root);

    CodeGenEnvironment codegen_env;
    Sema sema{std_scope, *root->scope, codegen_env};
    TypeCheckVisitor{sema}(root);

    bool has_error = Diagnostic::flush(sources);
    if (has_error) return EXIT_FAILURE;

    return codegen(sources, sema, codegen_env);
}
