#include "pch.hpp"

#include "ast.hpp"
#include "diagnosis.hpp"

class CFG {
public:
    struct Node {
        const Node* parent;
        GlobalMemory::Vector<ASTNodeVariant> statements;
        GlobalMemory::Vector<std::string_view> ending_variables;
        GlobalMemory::Vector<std::pair<ASTExprVariant, Node*>> successors;
    };

private:
    const Node* entry_node_;

public:
    explicit CFG(const Node* entry_node) : entry_node_(entry_node) {}

    ~CFG() noexcept {
        GlobalMemory::FlatSet<const Node*> visited;
        GlobalMemory::Vector<const Node*> remaining{entry_node_};
        while (!remaining.empty()) {
            const Node* current = remaining.back();
            remaining.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            std::ranges::transform(
                current->successors,
                std::back_inserter(remaining),
                &std::pair<ASTExprVariant, Node*>::second
            );
            delete current;
            visited.insert(current);
        }
    }

    auto variables(const Node* node, ASTNodeVariant statement)
        -> GlobalMemory::FlatSet<std::string_view> {
        GlobalMemory::FlatSet<std::string_view> beginning_variables;
        GlobalMemory::FlatSet<std::string_view> ending_variables;

        size_t index = static_cast<size_t>(std::distance(
            node->statements.begin(),
            std::find(node->statements.begin(), node->statements.end(), statement)
        ));

        for (size_t i = 0; i < index; ++i) {
            const auto& stmt = node->statements[i];
            if (const auto* var_decl = std::get_if<const ASTDeclaration*>(&stmt)) {
                beginning_variables.insert((*var_decl)->identifier);
            }
        }

        const Node* current_node = node;
        while (current_node->parent) {
            current_node = current_node->parent;
            for (const auto& stmt : current_node->statements) {
                if (const auto* var_decl = std::get_if<const ASTDeclaration*>(&stmt)) {
                    beginning_variables.insert((*var_decl)->identifier);
                }
            }
            ending_variables.insert(
                current_node->ending_variables.begin(), current_node->ending_variables.end()
            );
        }

        auto end = std::set_difference(
            beginning_variables.begin(),
            beginning_variables.end(),
            ending_variables.begin(),
            ending_variables.end(),
            beginning_variables.begin()
        );
        beginning_variables.erase(end, beginning_variables.end());
        return beginning_variables;
    }
};

class CFGBuilder {
private:
    inline static CFG::Node sentinel_;

private:
    CFG::Node return_node_;

public:
    auto build(const ASTFunctionDefinition* root) -> CFG {
        CFG::Node* root_node = new CFG::Node{nullptr, {}, {}, {}};
        for (const auto& stmt : root->body) {
            root_node = dispatch(stmt, root_node);
            if (const auto* decl = std::get_if<const ASTDeclaration*>(&stmt)) {
                root_node->ending_variables.insert(
                    root_node->ending_variables.begin(), (*decl)->identifier
                );
            }
        }
        return CFG(root_node);
    }

private:
    auto dispatch(const ASTNodeVariant& variant, CFG::Node* current) -> CFG::Node* {
        CFG::Node* next_node = std::visit(
            [current, this](const auto& node) { return (*this)(node, current); }, variant
        );
        if (next_node == &sentinel_) {
            current->statements.push_back(variant);
            return current;
        }
        return next_node;
    }

    auto operator()(std::monostate mono, CFG::Node* current) -> CFG::Node* { UNREACHABLE(); }

    auto operator()(const ASTNode* node, CFG::Node* current) -> CFG::Node* { return &sentinel_; }

    auto operator()(const ASTIfStatement* node, CFG::Node* current) -> CFG::Node* {
        CFG::Node* if_node = new CFG::Node{current, {}, {}, {}};
        current->successors.push_back({std::monostate{}, if_node});
        CFG::Node* final_if_node = (*this)(node->if_block, if_node);
        CFG::Node* after_if_node = new CFG::Node{current, {}, {}, {}};
        final_if_node->successors.push_back({std::monostate{}, after_if_node});
        if (!holds_monostate(node->else_block)) {
            CFG::Node* else_node = new CFG::Node{current, {}, {}, {}};
            current->successors.push_back({std::monostate{}, else_node});
            CFG::Node* final_else_node = dispatch(node->else_block, else_node);
            final_else_node->successors.push_back({std::monostate{}, after_if_node});
        }
        return after_if_node;
    }

    auto operator()(const ASTForStatement* node, CFG::Node* current) -> CFG::Node* {
        CFG::Node* for_node = new CFG::Node{current, {}, {}, {}};
        CFG::Node* after_for_node = new CFG::Node{current, {}, {}, {}};
        current->successors.push_back({node->condition, for_node});
        current->successors.push_back({std::monostate{}, after_for_node});
        CFG::Node* final_for_node = (*this)(node->body, for_node);
        final_for_node->successors.push_back({node->condition, for_node});
        final_for_node->successors.push_back({std::monostate{}, after_for_node});
        return after_for_node;
    }

    auto operator()(const ASTSwitchStatement* node, CFG::Node* current) -> CFG::Node* {
        CFG::Node* after_switch_node = new CFG::Node{current, {}, {}, {}};
        for (const auto& case_node : node->cases) {
            CFG::Node* case_cfg_node = new CFG::Node{current, {}, {}, {}};
            current->successors.push_back({case_node.value, case_cfg_node});
            CFG::Node* final_case_node = dispatch(case_node.body, case_cfg_node);
            final_case_node->successors.push_back({std::monostate{}, after_switch_node});
        }
        return after_switch_node;
    }

    auto operator()(const ASTLocalBlock* node, CFG::Node* current) -> CFG::Node* {
        CFG::Node* block_node = new CFG::Node{current, {}, {}, {}};
        current->successors.push_back({std::monostate{}, block_node});
        for (const auto& stmt : node->statements) {
            current = dispatch(stmt, block_node);
            if (const auto* decl = std::get_if<const ASTDeclaration*>(&stmt)) {
                block_node->ending_variables.insert(
                    block_node->ending_variables.begin(), (*decl)->identifier
                );
            }
        }
        CFG::Node* after_block_node = new CFG::Node{current, {}, {}, {}};
        current->successors.push_back({std::monostate{}, after_block_node});
        return after_block_node;
    }

    auto operator()(const ASTReturnStatement* node, CFG::Node* current) -> CFG::Node* {
        current->statements.push_back(node);
        return &return_node_;
    }
};
