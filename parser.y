%{
#include "pch.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "out/parser.tab.hpp"

extern int yylex(std::unique_ptr<ASTNode>* yylval, Location* yylloc);
%}

%language "C++"
%require "3.8"
%code requires {
    #include "ast.hpp"
}
%debug

%locations
%define api.value.type { std::unique_ptr<ASTNode> }
%define api.location.type { Location }

%parse-param { std::unique_ptr<ASTNode>& root }

/* TypeScript Keywords */
%token KW_CONST KW_LET KW_TYPE KW_INTEGER KW_FLOAT KW_STRING KW_BOOLEAN KW_NULL
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
        { root = std::move($statements); }
    ;

statements
    : statement
        { $$ = std::make_unique<ASTCodeBlock>(@$, std::move($statement)); }
    | statements[prev] statement
        {
            static_cast<ASTCodeBlock&>(*$prev).push(@$, std::move($statement));
            $$ = std::move($prev);
        }
    ;

statement
    : expression OP_SEMICOLON
        { $$ = std::move($expression); }
    | declaration OP_SEMICOLON
        { $$ = std::move($declaration); }
    | if_statement
        { $$ = std::move($if_statement); }
    | for_statement
        { $$ = std::move($for_statement); }
    | break_statement
        { $$ = std::move($break_statement); }
    | continue_statement
        { $$ = std::move($continue_statement); }
    ;

code_block
    : OP_LBRACE OP_RBRACE
        { $$ = std::make_unique<ASTCodeBlock>(@$); }
    | OP_LBRACE statements OP_RBRACE
        { $$ = std::move($statements); }
    ;

identifier
    : T_IDENTIFIER
        { $$ = std::make_unique<ASTIdentifier>(*static_cast<ASTToken*>($1.get())); }
    ;

constant
    : KW_NULL
        { $$ = std::make_unique<ASTConstant<NullValue>>(*static_cast<ASTToken*>($1.get())); }
    | T_INTEGER
        { $$ = std::make_unique<ASTConstant<IntegerValue>>(*static_cast<ASTToken*>($1.get())); }
    | T_FLOAT
        { $$ = std::make_unique<ASTConstant<FloatValue>>(*static_cast<ASTToken*>($1.get())); }
    | T_STRING
        { $$ = std::make_unique<ASTConstant<StringValue>>(*static_cast<ASTToken*>($1.get())); }
    | T_BOOLEAN
        { $$ = std::make_unique<ASTConstant<BooleanValue>>(*static_cast<ASTToken*>($1.get())); }
    ;

primitive_type
    : KW_NULL
        { $$ = std::make_unique<ASTPrimitiveType<NullType>>(*static_cast<ASTToken*>($1.get())); }
    | KW_INTEGER
        { $$ = std::make_unique<ASTPrimitiveType<IntegerType>>(*static_cast<ASTToken*>($1.get())); }
    | KW_FLOAT
        { $$ = std::make_unique<ASTPrimitiveType<FloatType>>(*static_cast<ASTToken*>($1.get())); }
    | KW_STRING
        { $$ = std::make_unique<ASTPrimitiveType<StringType>>(*static_cast<ASTToken*>($1.get())); }
    | KW_BOOLEAN
        { $$ = std::make_unique<ASTPrimitiveType<BooleanType>>(*static_cast<ASTToken*>($1.get())); }
    ;

type_expression
    : primitive_type
        { $$ = std::move($primitive_type); }
    // | function_type
    //     { $$ = static_cast<ASTTypeExpression*>($function_type); }
    // | object_type
    //     { $$ = static_cast<ASTTypeExpression*>($object_type); }
    // | intersection_type
    //     { $$ = static_cast<ASTTypeExpression*>($intersection_type); }
    // | union_type
    //     { $$ = static_cast<ASTTypeExpression*>($union_type); }

type_alias
    : KW_TYPE identifier OP_ASSIGN type_expression[type] OP_SEMICOLON
        {
            $$ = std::make_unique<ASTTypeAlias>(
                @$,
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())),
                std::move($type));
        }
    ;

argument_list
    : expr_without_comma[arg]
        {
            $$ = std::make_unique<ASTFunctionCallArguments>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($arg.release())));
        }
    | argument_list[prev] OP_COMMA expr_without_comma[arg]
        {
            auto& args = static_cast<ASTFunctionCallArguments&>(*$prev);
            args.push_back(
                @$,
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($arg.release())));
            $$ = std::move($prev);
        }
    ;

function_call
    : expr_without_comma[function] OP_LPAREN OP_RPAREN
        {
            $$ = std::make_unique<ASTFunctionCall>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($function.release())));
        }
    | expr_without_comma[function] OP_LPAREN argument_list OP_RPAREN
        {
            $$ = std::make_unique<ASTFunctionCall>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($function.release())), 
                std::unique_ptr<ASTFunctionCallArguments>(static_cast<ASTFunctionCallArguments*>($argument_list.release())));
        }
    ;

expr_without_comma
    : constant
        { $$ = std::move($constant); }
    | identifier
        { $$ = std::move($identifier); }
    | function_call
        { $$ = std::move($function_call); }
    | OP_LPAREN expr_without_comma[expr] OP_RPAREN
        { $$ = std::move($expr); }
    /* Unary operators */
    | OP_SUBTRACT expr_without_comma[expr] %prec OP_NEGATE
        { $$ = std::make_unique<ASTNegateOp>(@$, std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expr.release()))); }
    | OP_LOGICAL_NOT expr_without_comma[expr]
        { $$ = std::make_unique<ASTLogicalNotOp>(@$, std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expr.release()))); }
    | OP_BITWISE_NOT expr_without_comma[expr]
        { $$ = std::make_unique<ASTBitwiseNotOp>(@$, std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expr.release()))); }
    /* Arithmetic operators */
    | expr_without_comma[left] OP_ADD expr_without_comma[right]
        {
            $$ = std::make_unique<ASTAddOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_SUBTRACT expr_without_comma[right]
        {
            $$ = std::make_unique<ASTSubtractOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_MULTIPLY expr_without_comma[right]
        {
            $$ = std::make_unique<ASTMultiplyOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_DIVIDE expr_without_comma[right]
        {
            $$ = std::make_unique<ASTDivideOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_REMAINDER expr_without_comma[right]
        {
            $$ = std::make_unique<ASTRemainderOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Comparison operators */
    | expr_without_comma[left] OP_EQUAL expr_without_comma[right]
        {
            $$ = std::make_unique<ASTEqualOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_NOT_EQUAL expr_without_comma[right]
        {
            $$ = std::make_unique<ASTNotEqualOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_LESS_THAN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLessThanOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_GREATER_THAN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTGreaterThanOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_LESS_EQUAL expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLessEqualOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_GREATER_EQUAL expr_without_comma[right]
        {
            $$ = std::make_unique<ASTGreaterEqualOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Logical operators */
    | expr_without_comma[left] OP_LOGICAL_AND expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLogicalAndOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_LOGICAL_OR expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLogicalOrOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Bitwise operators */
    | expr_without_comma[left] OP_BITWISE_AND expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseAndOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_BITWISE_OR expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseOrOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_BITWISE_XOR expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseXorOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Shift operators */
    | expr_without_comma[left] OP_LEFT_SHIFT expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLeftShiftOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_RIGHT_SHIFT expr_without_comma[right]
        {
            $$ = std::make_unique<ASTRightShiftOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Assignment operators */
    | expr_without_comma[left] OP_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_ADD_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTAddAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_SUBTRACT_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTSubtractAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_MULTIPLY_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTMultiplyAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_DIVIDE_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTDivideAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_REMAINDER_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTRemainderAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_LEFT_SHIFT_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTLeftShiftAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_RIGHT_SHIFT_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTRightShiftAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_BITWISE_AND_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseAndAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_BITWISE_OR_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseOrAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    | expr_without_comma[left] OP_BITWISE_XOR_ASSIGN expr_without_comma[right]
        {
            $$ = std::make_unique<ASTBitwiseXorAssignOp>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($left.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($right.release())));
        }
    /* Other operators */
    // | expr_without_comma[left] OP_SCOPE_RESOLUTION identifier
    // | expr_without_comma[condition] OP_QUESTION expr_without_comma[then] OP_COLON expr_without_comma[else]
    // | expr_without_comma[left] OP_DOT identifier
    ;

expression
    : expr_without_comma
        { $$ = std::move($expr_without_comma); }
    | expr_without_comma OP_COMMA expr_without_comma
        { $$ = nullptr; /* TODO */ }
    ;

declaration
    : KW_CONST identifier OP_ASSIGN expression
        {
            $$ = std::make_unique<ASTDeclaration>(
                @$, 
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expression.release())));
        }
    | KW_LET identifier OP_ASSIGN expression
        {
            $$ = std::make_unique<ASTDeclaration>(
                @$, 
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expression.release())));
        }
    | KW_CONST identifier OP_COLON type_expression[type] OP_ASSIGN expression
        {
            $$ = std::make_unique<ASTDeclaration>(@$, 
                std::move($type),
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expression.release())));
        }
    | KW_LET identifier OP_COLON type_expression[type] OP_ASSIGN expression
        {
            $$ = std::make_unique<ASTDeclaration>(@$, 
                std::move($type),
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($expression.release())));
        }
    | KW_LET identifier OP_COLON type_expression[type]
        {
            $$ = std::make_unique<ASTDeclaration>(@$, 
                std::move($type),
                std::unique_ptr<ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())), nullptr);
        }
    ;

if_statement
    : KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then]
        {
            $$ = std::make_unique<ASTIfStatement>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($condition.release())), 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($then.release())), 
                nullptr);
        }
    | KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then] KW_ELSE code_block[else]
        {
            $$ = std::make_unique<ASTIfStatement>(
                @$, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($condition.release())), 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($then.release())), 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($else.release())));
        }
    ;

optional_initializer
    : /* empty */
        { $$ = nullptr; }
    | declaration
        { $$ = std::move($declaration); }
    | expression
        { $$ = std::move($expression); }
    ;

optional_condition
    : /* empty */
        { $$ = nullptr; }
    | expression
        { $$ = std::move($expression); }
    ;

optional_increment
    : /* empty */
        { $$ = nullptr; }
    | expression
        { $$ = std::move($expression); }
    ;

for_statement
    : KW_FOR OP_LPAREN optional_initializer OP_SEMICOLON optional_condition OP_SEMICOLON optional_increment OP_RPAREN code_block
        {
            $$ = std::make_unique<ASTForStatement>(
                @$, 
                std::move($optional_initializer), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($optional_condition.release())), 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($optional_increment.release())), 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($code_block.release())));
        }
    | KW_FOR OP_LPAREN expression[condition] OP_RPAREN code_block
        {
            $$ = std::make_unique<ASTForStatement>(
                @$, 
                nullptr, 
                std::unique_ptr<ASTExpression>(static_cast<ASTExpression*>($condition.release())), 
                nullptr, 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($code_block.release())));
        }
    | KW_FOR code_block
        {
            $$ = std::make_unique<ASTForStatement>(
                @$, 
                nullptr, 
                nullptr, 
                nullptr, 
                std::unique_ptr<ASTCodeBlock>(static_cast<ASTCodeBlock*>($code_block.release())));
        }
    ;

break_statement
    : KW_BREAK OP_SEMICOLON
        { $$ = std::make_unique<ASTBreakStatement>(@$); }
    ;

continue_statement
    : KW_CONTINUE OP_SEMICOLON
        { $$ = std::make_unique<ASTContinueStatement>(@$); }
    ;

parameter
    : identifier
        {
            $$ = std::make_unique<ASTFunctionParameter>(
                @$,
                std::unique_ptr<const ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())),
                nullptr);
        }
    | identifier OP_COLON type_expression[type]
        {
            $$ = std::make_unique<ASTFunctionParameter>(
                @$,
                std::unique_ptr<const ASTIdentifier>(static_cast<ASTIdentifier*>($identifier.release())),
                std::unique_ptr<const ASTExpression>(static_cast<ASTExpression*>($type.release())));
        }
    ;
