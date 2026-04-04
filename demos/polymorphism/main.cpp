#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

struct Node {
    virtual void to_rpn(std::vector<Node*>& out) = 0;
    virtual void eval(std::span<int64_t> stack, int& sp) = 0;
    virtual void eval(std::vector<int64_t>& stack) = 0;
    // We intentionally omit virtual destructor per the bump pointer specification.
};

struct ValueNode : Node {
    int64_t val;

    explicit ValueNode(int64_t v) : val(v) {}

    void to_rpn(std::vector<Node*>& out) override { out.push_back(this); }

    void eval(std::span<int64_t> stack, int& sp) override { stack[sp++] = val; }

    void eval(std::vector<int64_t>& stack) override { stack.push_back(val); }
};

struct AddNode : Node {
    Node* left;
    Node* right;

    AddNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] += stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] += stack.back();
        stack.pop_back();
    }
};

struct SubNode : Node {
    Node* left;
    Node* right;

    SubNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] -= stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] -= stack.back();
        stack.pop_back();
    }
};

struct MulNode : Node {
    Node* left;
    Node* right;

    MulNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] *= stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] *= stack.back();
        stack.pop_back();
    }
};

struct DivNode : Node {
    Node* left;
    Node* right;

    DivNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        int64_t r = stack[--sp];
        stack[sp - 1] /= (r == 0 ? 1 : r);
    }

    void eval(std::vector<int64_t>& stack) override {
        int64_t r = stack.back();
        stack.pop_back();
        stack.back() /= (r == 0 ? 1 : r);
    }
};

struct ModNode : Node {
    Node* left;
    Node* right;

    ModNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        int64_t r = stack[--sp];
        stack[sp - 1] %= (r == 0 ? 1 : r);
    }

    void eval(std::vector<int64_t>& stack) override {
        int64_t r = stack.back();
        stack.pop_back();
        stack.back() %= (r == 0 ? 1 : r);
    }
};

struct AndNode : Node {
    Node* left;
    Node* right;

    AndNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] &= stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] &= stack.back();
        stack.pop_back();
    }
};

struct OrNode : Node {
    Node* left;
    Node* right;

    OrNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] |= stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] |= stack.back();
        stack.pop_back();
    }
};

struct XorNode : Node {
    Node* left;
    Node* right;

    XorNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] ^= stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] ^= stack.back();
        stack.pop_back();
    }
};

struct GtNode : Node {
    Node* left;
    Node* right;

    GtNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] = stack[sp - 2] > stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] = stack[stack.size() - 2] > stack.back();
        stack.pop_back();
    }
};

struct EqNode : Node {
    Node* left;
    Node* right;

    EqNode(Node* l, Node* r) : left(l), right(r) {}

    void to_rpn(std::vector<Node*>& out) override {
        left->to_rpn(out);
        right->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        stack[sp - 2] = stack[sp - 2] == stack[sp - 1];
        --sp;
    }

    void eval(std::vector<int64_t>& stack) override {
        stack[stack.size() - 2] = stack[stack.size() - 2] == stack.back();
        stack.pop_back();
    }
};

struct CondNode : Node {
    Node* cond;
    Node* a;
    Node* b;

    CondNode(Node* c, Node* lhs, Node* rhs) : cond(c), a(lhs), b(rhs) {}

    void to_rpn(std::vector<Node*>& out) override {
        cond->to_rpn(out);
        a->to_rpn(out);
        b->to_rpn(out);
        out.push_back(this);
    }

    void eval(std::span<int64_t> stack, int& sp) override {
        int64_t f = stack[--sp];
        int64_t t = stack[--sp];
        int64_t c = stack[--sp];
        stack[sp++] = c ? t : f;
    }

    void eval(std::vector<int64_t>& stack) override {
        int64_t f = stack.back();
        stack.pop_back();
        int64_t t = stack.back();
        stack.pop_back();
        int64_t c = stack.back();
        stack.pop_back();
        stack.push_back(c ? t : f);
    }
};

struct Arena {
    std::vector<std::byte> storage;
    size_t offset = 0;

    explicit Arena(size_t size) : storage(size) {}

    template <typename T>
    T* alloc(T value) {
        const size_t align = alignof(T);
        offset = (offset + align - 1) & ~(align - 1);
        if (offset + sizeof(T) > storage.size()) {
            throw std::bad_alloc();
        }

        auto* ptr = reinterpret_cast<T*>(storage.data() + offset);
        std::construct_at(ptr, std::move(value));
        offset += sizeof(T);
        return ptr;
    }
};

Node* generate_ast(
    Arena& arena, std::mt19937& rng, int& current_nodes, int max_nodes, int depth, int max_depth
) {
    if (depth >= max_depth || current_nodes >= max_nodes) {
        ++current_nodes;
        return arena.alloc<ValueNode>(
            ValueNode(std::uniform_int_distribution<int64_t>(1, 100)(rng))
        );
    }

    std::uniform_int_distribution<int> type_dist(0, 100);
    if (type_dist(rng) < 5) {
        ++current_nodes;
        return arena.alloc<ValueNode>(
            ValueNode(std::uniform_int_distribution<int64_t>(1, 100)(rng))
        );
    }

    std::uniform_int_distribution<int> op_dist(0, 10);
    const int op = op_dist(rng);
    ++current_nodes;

    if (op == 10) {
        Node* cond = generate_ast(arena, rng, current_nodes, max_nodes, depth + 1, max_depth);
        Node* a = generate_ast(arena, rng, current_nodes, max_nodes, depth + 1, max_depth);
        Node* b = generate_ast(arena, rng, current_nodes, max_nodes, depth + 1, max_depth);
        return arena.alloc<CondNode>(CondNode(cond, a, b));
    }

    Node* left = generate_ast(arena, rng, current_nodes, max_nodes, depth + 1, max_depth);
    Node* right = generate_ast(arena, rng, current_nodes, max_nodes, depth + 1, max_depth);
    switch (op) {
    case 0:
        return arena.alloc<AddNode>(AddNode(left, right));
    case 1:
        return arena.alloc<SubNode>(SubNode(left, right));
    case 2:
        return arena.alloc<MulNode>(MulNode(left, right));
    case 3:
        return arena.alloc<DivNode>(DivNode(left, right));
    case 4:
        return arena.alloc<ModNode>(ModNode(left, right));
    case 5:
        return arena.alloc<AndNode>(AndNode(left, right));
    case 6:
        return arena.alloc<OrNode>(OrNode(left, right));
    case 7:
        return arena.alloc<XorNode>(XorNode(left, right));
    case 8:
        return arena.alloc<GtNode>(GtNode(left, right));
    case 9:
        return arena.alloc<EqNode>(EqNode(left, right));
    default:
        return arena.alloc<ValueNode>(ValueNode(1));
    }
}

int main() {
    constexpr size_t ARENA_SIZE = 256 * 1024 * 1024;
    Arena arena(ARENA_SIZE);

    std::mt19937 rng(42);
    int current_nodes = 0;
    int max_nodes = 1'000'000;
    int max_depth = 50;

    std::cout << "Generating AST..." << std::endl;
    Node* root = generate_ast(arena, rng, current_nodes, max_nodes, 0, max_depth);
    std::cout << "Nodes generated: " << current_nodes << std::endl;

    std::vector<Node*> rpn;
    rpn.reserve(current_nodes);
    root->to_rpn(rpn);
    std::cout << "RPN generation complete. Size: " << rpn.size() << std::endl;

    std::vector<int64_t> warmup_stack;
    size_t max_stack_size = 0;
    for (Node* node : rpn) {
        node->eval(warmup_stack);
        if (warmup_stack.size() > max_stack_size) {
            max_stack_size = warmup_stack.size();
        }
    }

    std::cout << "Warm-up output: " << warmup_stack[0] << std::endl;
    std::cout << "Max stack size needed: " << max_stack_size << std::endl;

    std::vector<int64_t> stack;
    stack.resize(max_stack_size + 10);
    std::span<int64_t> stack_span(stack);

    int benchmark_iterations = 2000;
    std::cout << "Running benchmark on " << benchmark_iterations << " iterations..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    int64_t final_res = 0;

    for (int i = 0; i < benchmark_iterations; ++i) {
        int sp = 0;
        for (Node* node : rpn) {
            node->eval(stack_span, sp);
        }
        final_res ^= stack[0];
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(end - start).count();
    double total_nodes_eval = static_cast<double>(rpn.size()) * benchmark_iterations;

    std::cout << "Total time: " << total_time << " s\n";
    std::cout << "Throughput: " << (total_nodes_eval / total_time) / 1e6 << " million nodes/sec\n";
    std::cout << "Final Result Checksum: " << final_res << "\n";

    constexpr int dynamic_cast_iterations = 512;
    auto cast_start = std::chrono::high_resolution_clock::now();
    uint64_t value_node_xor = 0;
    uint64_t value_node_single_pass_xor = 0;
    size_t value_node_count = 0;
    for (int iter = 0; iter < dynamic_cast_iterations; ++iter) {
        for (Node* node : rpn) {
            if (auto* value_node = dynamic_cast<ValueNode*>(node); value_node != nullptr) {
                if (iter == 0) {
                    ++value_node_count;
                    value_node_single_pass_xor ^= static_cast<uint64_t>(value_node->val);
                }
                value_node_xor ^= static_cast<uint64_t>(value_node->val);
            }
        }
    }
    auto cast_end = std::chrono::high_resolution_clock::now();
    double dynamic_cast_time = std::chrono::duration<double>(cast_end - cast_start).count();
    double total_dynamic_cast_checks = static_cast<double>(rpn.size()) * dynamic_cast_iterations;
    double dynamic_cast_throughput =
        dynamic_cast_time > 0.0 ? (total_dynamic_cast_checks / dynamic_cast_time) / 1e6 : 0.0;

    std::cout << "ValueNode count: " << value_node_count << "\n";
    std::cout << "ValueNode single-pass XOR (hex): 0x" << std::hex << value_node_single_pass_xor
              << std::dec << "\n";
    std::cout << "DynamicCast loop time: " << dynamic_cast_time << " s\n";
    std::cout << "DynamicCast throughput: " << dynamic_cast_throughput << " million checks/sec\n";
    std::cout << "ValueNode DynamicCast XOR (hex): 0x" << std::hex << value_node_xor << std::dec
              << "\n";

    return 0;
}
