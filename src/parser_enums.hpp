namespace Parser {
enum class StatementKind {
    LocalBlock = 1,
    ExprStatement,
    DeclarationStatement,
    IfStatement,
    SwitchStatement,
    MatchStatement,
    ForStatement,
    BreakStatement,
    ContinueStatement,
    ReturnStatement,
    TypeAlias,
    ThrowStatement,
    AwaitStatement,
    YieldStatement,
};

enum class MatchCaseKind {
    Normal = 1,
    Default,
};

enum class ForStatementKind {
    CStyle = 1,
    WhileStyle,
};

enum class ParameterKind {
    Self = 1,
    Normal,
    Default,
    Variadic,
};

enum class ExprKind {
    Self = 1,
    SelfType,
    Const,
    Identifier,
    Instantiation,
    MemberAccess,
    PointerAccess,
    IndexAccess,
    Call,
    StructInit,
    ArrayInit,
    Slice,
    Move,
    Forward,
    Paren,
    Lambda,
    Unary,
    PostfixUnary,
    As,
    Multiplicative,
    Additive,
    Shift,
    Relational,
    Equality,
    BitAnd,
    BitXor,
    BitOr,
    And,
    Or,
    Ternary,
    Assign
};

enum class TypeKind {
    Self = 1,
    Primitive,
    Identifier,
    Instantiated,
    MemberAccess,
    Struct,
    Array,
    Function,
    Reference,
    Pointer,
    Dynamic,
    Paren,
};
}  // namespace Parser
