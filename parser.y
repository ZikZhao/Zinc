%{
#include "pch.hpp"
#include "ast.hpp"
#include "value.hpp"
#include "out/parser.tab.hpp"

extern int yylex(ASTNode** yylval, Location* yylloc);
%}

%language "C++"
%require "3.2"
%code requires {
    #include "ast.hpp"
}
%debug

%locations
%define api.value.type { ASTNode* }
%define api.location.type { Location }

%parse-param { ASTNode*& root }

/* TypeScript Keywords */
%token KW_CONST KW_LET KW_INTEGER KW_FLOAT KW_STRING KW_BOOLEAN KW_NULL
%token KW_IF KW_ELSE KW_FOR KW_WHILE KW_FUNCTION KW_RETURN KW_CLASS KW_EXTENDS
%token KW_BREAK KW_CONTINUE KW_SWITCH KW_CASE KW_DEFAULT KW_IMPORT KW_FROM KW_AS

/* Arithmetic Operators */
%token OP_ADD OP_SUBTRACT OP_MULTIPLY OP_DIVIDE OP_REMAINDER
%token OP_INCREMENT OP_DECREMENT

/* Comparison Operators */
%token OP_EQUAL OP_NOT_EQUAL OP_LESS_THAN OP_GREATER_THAN OP_LESS_EQUAL OP_GREATER_EQUAL

/* Logical Operators */
%token OP_LOGICAL_AND OP_LOGICAL_OR OP_LOGICAL_NOT

/* Bitwise Operators */
%token OP_BITWISE_AND OP_BITWISE_OR OP_BITWISE_XOR OP_BITWISE_NOT
%token OP_LEFT_SHIFT OP_RIGHT_SHIFT

/* Assignment Operators */
%token OP_ASSIGN
%token OP_ADD_ASSIGN OP_SUBTRACT_ASSIGN OP_MULTIPLY_ASSIGN OP_DIVIDE_ASSIGN OP_REMAINDER_ASSIGN
%token OP_LEFT_SHIFT_ASSIGN OP_RIGHT_SHIFT_ASSIGN
%token OP_BITWISE_AND_ASSIGN OP_BITWISE_OR_ASSIGN OP_BITWISE_XOR_ASSIGN

/* Other Operators */
%token OP_SCOPE_RESOLUTION OP_QUESTION OP_COLON OP_ARROW

/* Punctuation */
%token OP_SEMICOLON OP_COMMA OP_DOT

/* Brackets and Braces */
%token OP_LPAREN OP_RPAREN OP_LBRACKET OP_RBRACKET OP_LBRACE OP_RBRACE

/* Template Literals */
%token OP_TEMPLATE_STRING

/* Literals and Identifiers */
%token T_INTEGER T_FLOAT T_STRING T_BOOLEAN T_NULL
%token T_IDENTIFIER

/* Comments */
%token T_SINGLE_LINE_COMMENT T_MULTI_LINE_COMMENT

/* Precedence and Associativity */
%left OP_COMMA
%right OP_QUESTION OP_COLON OP_ASSIGN OP_ADD_ASSIGN OP_SUBTRACT_ASSIGN OP_MULTIPLY_ASSIGN OP_DIVIDE_ASSIGN OP_REMAINDER_ASSIGN OP_EXP_ASSIGN OP_LEFT_SHIFT_ASSIGN OP_RIGHT_SHIFT_ASSIGN OP_BITWISE_AND_ASSIGN OP_BITWISE_OR_ASSIGN OP_BITWISE_XOR_ASSIGN
%left OP_LOGICAL_OR
%left OP_LOGICAL_AND
%left OP_BITWISE_OR
%left OP_BITWISE_XOR
%left OP_BITWISE_AND
%left OP_EQUAL OP_NOT_EQUAL 
%left OP_LESS_THAN OP_GREATER_THAN OP_LESS_EQUAL OP_GREATER_EQUAL
%left OP_LEFT_SHIFT OP_RIGHT_SHIFT OP_UNSIGNED_RIGHT_SHIFT
%left OP_ADD OP_SUBTRACT
%left OP_MULTIPLY OP_DIVIDE OP_REMAINDER
%right OP_EXP
%right OP_INCREMENT OP_DECREMENT OP_NEGATE OP_LOGICAL_NOT OP_BITWISE_NOT
%left OP_DOT OP_LPAREN OP_INDEXING
%left OP_SCOPE_RESOLUTION
%%

top_level_statements
    : /* empty */
        { root = nullptr; }
    | statements
        { root = $statements; }
    ;

statements
    : statement
        { $$ = new ASTStatements(@$, $statement); }
    | statements[prev] statement
        { $$ = &static_cast<ASTStatements*>($prev)->push(@$, $statement); }
    ;

statement
    : expression OP_SEMICOLON
        { $$ = $expression; }
    | declaration OP_SEMICOLON
        { $$ = $declaration; }
    | if_statement
        { $$ = $if_statement; }
    | for_statement
        { $$ = $for_statement; }
    | break_statement
        { $$ = $break_statement; }
    | continue_statement
        { $$ = $continue_statement; }
    ;

code_block
    : OP_LBRACE OP_RBRACE
        { $$ = new ASTStatements(@$); }
    | OP_LBRACE statements OP_RBRACE
        { $$ = $statements; }
    ;

identifier
    : T_IDENTIFIER
        { $$ = new ASTIdentifier(*static_cast<ASTToken*>($1)); }
    ;

constant
    : KW_NULL
        { $$ = new ASTConstant<NullValue>(*static_cast<ASTToken*>($1)); }
    | T_INTEGER
        { $$ = new ASTConstant<IntegerValue>(*static_cast<ASTToken*>($1)); }
    | T_FLOAT
        { $$ = new ASTConstant<FloatValue>(*static_cast<ASTToken*>($1)); }
    | T_STRING
        { $$ = new ASTConstant<StringValue>(*static_cast<ASTToken*>($1)); }
    | T_BOOLEAN
        { $$ = new ASTConstant<BooleanValue>(*static_cast<ASTToken*>($1)); }
    ;

type
    : KW_INTEGER
    | KW_FLOAT
    | KW_STRING
    | KW_BOOLEAN
    ;

argument_list
    : expr_without_comma[arg]
        {
            $$ = new ASTFunctionCallArguments(@$, 
                static_cast<ASTValueExpression*>($arg));
        }
    | argument_list[prev] OP_COMMA expr_without_comma[arg]
        {
            $$ = &static_cast<ASTFunctionCallArguments*>($prev)->push_back(@$,
                static_cast<ASTValueExpression*>($arg));
        }
    ;

function_call
    : expr_without_comma[function] OP_LPAREN OP_RPAREN
        {
            $$ = new ASTFunctionCall(@$, 
                static_cast<ASTValueExpression*>($function));
        }
    | expr_without_comma[function] OP_LPAREN argument_list OP_RPAREN
        {
            $$ = new ASTFunctionCall(@$, 
                static_cast<ASTValueExpression*>($function), 
                static_cast<ASTFunctionCallArguments*>($argument_list));
        }
    ;

expr_without_comma
    : constant
        { $$ = $constant; }
    | identifier
        { $$ = $identifier; }
    | function_call
        { $$ = static_cast<ASTFunctionCall*>($function_call); }
    | OP_LPAREN expr_without_comma[expr] OP_RPAREN
        { $$ = static_cast<ASTValueExpression*>($expr); }
    /* Unary operators */
    | OP_SUBTRACT expr_without_comma[expr] %prec OP_NEGATE
        { $$ = new ASTNegOp(@$, static_cast<ASTValueExpression*>($expr)); }
    | OP_LOGICAL_NOT expr_without_comma[expr]
        { $$ = new ASTLogicalNotOp(@$, static_cast<ASTValueExpression*>($expr)); }
    | OP_BITWISE_NOT expr_without_comma[expr]
        { $$ = new ASTBitwiseNotOp(@$, static_cast<ASTValueExpression*>($expr)); }
    /* Arithmetic operators */
    | expr_without_comma[left] OP_ADD expr_without_comma[right]
        { $$ = new ASTAddOp(@$, static_cast<ASTValueExpression*>($left), 
                           static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_SUBTRACT expr_without_comma[right]
        { $$ = new ASTSubOp(@$, static_cast<ASTValueExpression*>($left), 
                           static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_MULTIPLY expr_without_comma[right]
        { $$ = new ASTMulOp(@$, static_cast<ASTValueExpression*>($left), 
                           static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_DIVIDE expr_without_comma[right]
        { $$ = new ASTDivOp(@$, static_cast<ASTValueExpression*>($left), 
                           static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_REMAINDER expr_without_comma[right]
        { $$ = new ASTRemOp(@$, static_cast<ASTValueExpression*>($left), 
                           static_cast<ASTValueExpression*>($right)); }
    /* Comparison operators */
    | expr_without_comma[left] OP_EQUAL expr_without_comma[right]
        { $$ = new ASTEqualOp(@$, static_cast<ASTValueExpression*>($left), 
                             static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_NOT_EQUAL expr_without_comma[right]
        { $$ = new ASTNotEqualOp(@$, static_cast<ASTValueExpression*>($left), 
                                static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_LESS_THAN expr_without_comma[right]
        { $$ = new ASTLessThanOp(@$, static_cast<ASTValueExpression*>($left), 
                                static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_GREATER_THAN expr_without_comma[right]
        { $$ = new ASTGreaterThanOp(@$, static_cast<ASTValueExpression*>($left), 
                                   static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_LESS_EQUAL expr_without_comma[right]
        { $$ = new ASTLessEqualOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_GREATER_EQUAL expr_without_comma[right]
        { $$ = new ASTGreaterEqualOp(@$, static_cast<ASTValueExpression*>($left), 
                                    static_cast<ASTValueExpression*>($right)); }
    /* Logical operators */
    | expr_without_comma[left] OP_LOGICAL_AND expr_without_comma[right]
        { $$ = new ASTLogicalAndOp(@$, static_cast<ASTValueExpression*>($left), 
                                  static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_LOGICAL_OR expr_without_comma[right]
        { $$ = new ASTLogicalOrOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    /* Bitwise operators */
    | expr_without_comma[left] OP_BITWISE_AND expr_without_comma[right]
        { $$ = new ASTBitwiseAndOp(@$, static_cast<ASTValueExpression*>($left), 
                                  static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_BITWISE_OR expr_without_comma[right]
        { $$ = new ASTBitwiseOrOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_BITWISE_XOR expr_without_comma[right]
        { $$ = new ASTBitwiseXorOp(@$, static_cast<ASTValueExpression*>($left), 
                                  static_cast<ASTValueExpression*>($right)); }
    /* Shift operators */
    | expr_without_comma[left] OP_LEFT_SHIFT expr_without_comma[right]
        { $$ = new ASTLeftShiftOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_RIGHT_SHIFT expr_without_comma[right]
        { $$ = new ASTRightShiftOp(@$, static_cast<ASTValueExpression*>($left), 
                                  static_cast<ASTValueExpression*>($right)); }
    /* Assignment operators */
    | expr_without_comma[left] OP_ASSIGN expr_without_comma[right]
        { $$ = new ASTAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                              static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_ADD_ASSIGN expr_without_comma[right]
        { $$ = new ASTAddAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_SUBTRACT_ASSIGN expr_without_comma[right]
        { $$ = new ASTSubAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_MULTIPLY_ASSIGN expr_without_comma[right]
        { $$ = new ASTMulAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_DIVIDE_ASSIGN expr_without_comma[right]
        { $$ = new ASTDivAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_REMAINDER_ASSIGN expr_without_comma[right]
        { $$ = new ASTRemAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                 static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_LEFT_SHIFT_ASSIGN expr_without_comma[right]
        { $$ = new ASTLeftShiftAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                       static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_RIGHT_SHIFT_ASSIGN expr_without_comma[right]
        { $$ = new ASTRightShiftAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                        static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_BITWISE_AND_ASSIGN expr_without_comma[right]
        { $$ = new ASTBitwiseAndAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                        static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_BITWISE_OR_ASSIGN expr_without_comma[right]
        { $$ = new ASTBitwiseOrAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                       static_cast<ASTValueExpression*>($right)); }
    | expr_without_comma[left] OP_BITWISE_XOR_ASSIGN expr_without_comma[right]
        { $$ = new ASTBitwiseXorAssignOp(@$, static_cast<ASTValueExpression*>($left), 
                                        static_cast<ASTValueExpression*>($right)); }
    /* Other operators */
    | expr_without_comma[left] OP_SCOPE_RESOLUTION identifier
    | expr_without_comma[condition] OP_QUESTION expr_without_comma[then] OP_COLON expr_without_comma[else]
    | expr_without_comma[left] OP_DOT identifier
    ;

expression
    : expr_without_comma
        { $$ = $expr_without_comma; }
    | expr_without_comma OP_COMMA expr_without_comma
    ;

declaration
    : KW_CONST identifier OP_ASSIGN expression
        {
            $$ = new ASTDeclaration(@$, static_cast<ASTIdentifier*>($identifier), 
                                   static_cast<ASTValueExpression*>($expression));
        }
    | KW_LET identifier OP_ASSIGN expression
        {
            $$ = new ASTDeclaration(@$, static_cast<ASTIdentifier*>($identifier), 
                                   static_cast<ASTValueExpression*>($expression));
        }
    | KW_CONST identifier OP_COLON type OP_ASSIGN expression
        {
            $$ = new ASTDeclaration(@$, 
                static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                static_cast<ASTIdentifier*>($identifier), 
                static_cast<ASTValueExpression*>($expression));
        }
    | KW_LET identifier OP_COLON type OP_ASSIGN expression
        {
            $$ = new ASTDeclaration(@$, 
                static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                static_cast<ASTIdentifier*>($identifier), 
                static_cast<ASTValueExpression*>($expression));
        }
    | KW_LET identifier OP_COLON type
        {
            $$ = new ASTDeclaration(@$, 
                static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                static_cast<ASTIdentifier*>($identifier), nullptr);
        }
    ;

if_statement
    : KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then]
        {
            $$ = new ASTIfStatement(@$, static_cast<ASTValueExpression*>($condition), 
                                   static_cast<ASTStatements*>($then), nullptr);
        }
    | KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then] KW_ELSE code_block[else]
        {
            $$ = new ASTIfStatement(@$, static_cast<ASTValueExpression*>($condition), 
                                   static_cast<ASTStatements*>($then), 
                                   static_cast<ASTStatements*>($else));
        }
    ;

optional_initializer
    : /* empty */
        { $$ = nullptr; }
    | declaration
        { $$ = $declaration; }
    | expression
        { $$ = $expression; }
    ;

optional_condition
    : /* empty */
        { $$ = nullptr; }
    | expression
        { $$ = $expression; }
    ;

optional_increment
    : /* empty */
        { $$ = nullptr; }
    | expression
        { $$ = $expression; }
    ;

for_statement
    : KW_FOR OP_LPAREN optional_initializer OP_SEMICOLON optional_condition OP_SEMICOLON optional_increment OP_RPAREN code_block
        {
            $$ = new ASTForStatement(@$, $optional_initializer, 
                                    static_cast<ASTValueExpression*>($optional_condition), 
                                    static_cast<ASTValueExpression*>($optional_increment), 
                                    static_cast<ASTStatements*>($code_block));
        }
    | KW_FOR OP_LPAREN expression[condition] OP_RPAREN code_block
        {
            $$ = new ASTForStatement(@$, nullptr, 
                                    static_cast<ASTValueExpression*>($condition), 
                                    nullptr, static_cast<ASTStatements*>($code_block));
        }
    | KW_FOR code_block
        {
            $$ = new ASTForStatement(@$, nullptr, nullptr, nullptr, 
                                    static_cast<ASTStatements*>($code_block));
        }
    ;

break_statement
    : KW_BREAK OP_SEMICOLON
        { $$ = new ASTBreakStatement(@$); }
    ;

continue_statement
    : KW_CONTINUE OP_SEMICOLON
        { $$ = new ASTContinueStatement(@$); }
    ;

parameter
    : identifier
        {
            $$ = new ASTFunctionParameter(static_cast<ASTIdentifier*>($identifier));
        }
    | identifier OP_COLON type
        {
            $$ = new ASTFunctionParameter(
                static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)), 
                static_cast<ASTIdentifier*>($identifier));
        }
    ;

%%
std::ifstream yyin;

void yy::parser::error(const Location& location, const std::string& message) {
    std::cerr << "Parsing terminated due to errors: " << message << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        yyin.open(argv[1]);
        if (yyin.fail()) {
            std::cerr << "Error: Cannot open file " << argv[1] << std::endl;
            return 1;
        }
    }

    ASTNode* root = nullptr;
    yy::parser parser(root);
    parser.set_debug_level(1);
    parser.parse();

    std::cout << std::endl;
    root->print(std::cout);

    Context globals = Builtins;
    root->execute(globals, globals);
    delete root;
}