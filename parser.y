%{
#include <stdio.h>
#include <fstream>
#include <vector>
#include "ast.hpp"
#include "out/parser.tab.hpp"

std::vector<ASTNode*> nodes;

extern int yylex(ASTNode** yylval, Location* yylloc);
%}

%language "C++"
%require "3.2"
%code requires {
    #include "ast.hpp"
}

%locations
%define api.value.type { ASTNode* }
%define api.location.type { Location }

%parse-param { ASTNode*& root }

/* TypeScript Keywords */
%token KW_CONST KW_LET KW_INTEGER KW_FLOAT KW_STRING KW_BOOLEAN KW_NULL
%token KW_IF KW_ELSE KW_FOR KW_WHILE KW_FUNCTION KW_RETURN KW_CLASS KW_EXTENDS

/* Arithmetic Operators */
%token OP_ADD OP_SUB OP_MUL OP_DIV OP_REM OP_EXP
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
%token OP_ADD_ASSIGN OP_SUB_ASSIGN OP_MUL_ASSIGN OP_DIV_ASSIGN OP_REM_ASSIGN OP_EXP_ASSIGN
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
%right OP_QUESTION OP_COLON OP_ASSIGN OP_ADD_ASSIGN OP_SUB_ASSIGN OP_MUL_ASSIGN OP_DIV_ASSIGN OP_REM_ASSIGN OP_EXP_ASSIGN OP_LEFT_SHIFT_ASSIGN OP_RIGHT_SHIFT_ASSIGN OP_BITWISE_AND_ASSIGN OP_BITWISE_OR_ASSIGN OP_BITWISE_XOR_ASSIGN
%left OP_LOGICAL_OR
%left OP_LOGICAL_AND
%left OP_BITWISE_OR
%left OP_BITWISE_XOR
%left OP_BITWISE_AND
%left OP_EQUAL OP_NOT_EQUAL 
%left OP_LESS_THAN OP_GREATER_THAN OP_LESS_EQUAL OP_GREATER_EQUAL
%left OP_LEFT_SHIFT OP_RIGHT_SHIFT OP_UNSIGNED_RIGHT_SHIFT
%left OP_ADD OP_SUB
%left OP_MUL OP_DIV OP_REM
%right OP_EXP
%right OP_INCREMENT OP_DECREMENT OP_NEG OP_LOGICAL_NOT OP_BITWISE_NOT
%left OP_DOT OP_FUNCTION_CALL OP_INDEXING
%left OP_SCOPE_RESOLUTION
%%

top_level_statements : /* empty */   { /* no-op */ }
                     | statements    { root = $statements; }
                     ;

statements : statement                     { $$ = new ASTStatements(@$, $statement); }
           | statements[prev] statement    { $$ = &static_cast<ASTStatements*>($prev)->push(@$, $statement); }
           ;

statement : expression OP_SEMICOLON      { $$ = $expression; }
          | declaration OP_SEMICOLON     { $$ = $declaration; }
          | if_statement                 { $$ = $if_statement; }
          ;

code_block : OP_LBRACE OP_RBRACE               { $$ = new ASTStatements(@$); }
           | OP_LBRACE statements OP_RBRACE    { $$ = $statements; }
           ;

identifier : T_IDENTIFIER    { $$ = new ASTIdentifier(*static_cast<ASTToken*>($1)); }
           ;

constant : T_INTEGER    { $$ = new ASTConstant(*static_cast<ASTToken*>($1)); }
         | T_FLOAT      { $$ = new ASTConstant(*static_cast<ASTToken*>($1)); }
         | T_STRING     { $$ = new ASTConstant(*static_cast<ASTToken*>($1)); }
         | T_BOOLEAN    { $$ = new ASTConstant(*static_cast<ASTToken*>($1)); }
         | KW_NULL      { $$ = new ASTConstant(*static_cast<ASTToken*>($1)); }
         ;

type : KW_INTEGER
     | KW_FLOAT
     | KW_STRING
     | KW_BOOLEAN
     ;

expression : constant                                                             { $$ = $constant; }
           | identifier                                                           { $$ = $identifier; }
           | OP_SUB expression[expr] %prec OP_NEG                                 { $$ = new ASTNegOp(@$, static_cast<ASTValueExpression*>($expr)); }
           | expression[left] OP_ADD expression[right]                            { $$ = new ASTAddOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_SUB expression[right]                            { $$ = new ASTSubOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_MUL expression[right]                            { $$ = new ASTMulOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_DIV expression[right]                            { $$ = new ASTDivOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_REM expression[right]                            { $$ = new ASTRemOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_EXP expression[right]                            { $$ = new ASTExpOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_EQUAL expression[right]                          { $$ = new ASTEqualOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_NOT_EQUAL expression[right]                      { $$ = new ASTNotEqualOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_LESS_THAN expression[right]                      { $$ = new ASTLessThanOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_GREATER_THAN expression[right]                   { $$ = new ASTGreaterThanOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_LESS_EQUAL expression[right]                     { $$ = new ASTLessEqualOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_GREATER_EQUAL expression[right]                  { $$ = new ASTGreaterEqualOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_LOGICAL_AND expression[right]                    { $$ = new ASTLogicalAndOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_LOGICAL_OR expression[right]                     { $$ = new ASTLogicalOrOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | OP_LOGICAL_NOT expression[expr]                                      { $$ = new ASTLogicalNotOp(@$, static_cast<ASTValueExpression*>($expr)); }
           | expression[left] OP_BITWISE_AND expression[right]                    { $$ = new ASTBitwiseAndOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_BITWISE_OR expression[right]                     { $$ = new ASTBitwiseOrOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_BITWISE_XOR expression[right]                    { $$ = new ASTBitwiseXorOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | OP_BITWISE_NOT expression[expr]                                      { $$ = new ASTBitwiseNotOp(@$, static_cast<ASTValueExpression*>($expr)); }
           | expression[left] OP_LEFT_SHIFT expression[right]                     { $$ = new ASTLeftShiftOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_RIGHT_SHIFT expression[right]                    { $$ = new ASTRightShiftOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_ASSIGN expression[right]                         { $$ = new ASTAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_ADD_ASSIGN expression[right]                     { $$ = new ASTAddAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_SUB_ASSIGN expression[right]                     { $$ = new ASTSubAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_MUL_ASSIGN expression[right]                     { $$ = new ASTMulAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_DIV_ASSIGN expression[right]                     { $$ = new ASTDivAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_REM_ASSIGN expression[right]                     { $$ = new ASTRemAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_EXP_ASSIGN expression[right]                     { $$ = new ASTExpAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_LEFT_SHIFT_ASSIGN expression[right]              { $$ = new ASTLeftShiftAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_RIGHT_SHIFT_ASSIGN expression[right]             { $$ = new ASTRightShiftAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_BITWISE_AND_ASSIGN expression[right]             { $$ = new ASTBitwiseAndAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_BITWISE_OR_ASSIGN expression[right]              { $$ = new ASTBitwiseOrAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_BITWISE_XOR_ASSIGN expression[right]             { $$ = new ASTBitwiseXorAssignOp(@$, static_cast<ASTValueExpression*>($left), static_cast<ASTValueExpression*>($right)); }
           | expression[left] OP_SCOPE_RESOLUTION identifier
           | expression[condition] OP_QUESTION expression[then] OP_COLON expression[else]
           | expression[left] OP_DOT identifier
           | expression[left] OP_COMMA expression[right]
           | OP_LPAREN expression[expr] OP_RPAREN                                 { $$ = static_cast<ASTValueExpression*>($expr); }
           ;

declaration : KW_CONST identifier OP_ASSIGN expression
            {
                $$ = new ASTDeclaration(@$, static_cast<ASTIdentifier*>($identifier), static_cast<ASTValueExpression*>($expression));
            }
            | KW_LET identifier OP_ASSIGN expression
            {
                $$ = new ASTDeclaration(@$, static_cast<ASTIdentifier*>($identifier), static_cast<ASTValueExpression*>($expression));
            }
            | KW_CONST identifier OP_COLON type OP_ASSIGN expression
            {
                $$ = new ASTDeclaration(@$, static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                    static_cast<ASTIdentifier*>($identifier), static_cast<ASTValueExpression*>($expression));
            }
            | KW_LET identifier OP_COLON type OP_ASSIGN expression
            {
                $$ = new ASTDeclaration(@$, static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                    static_cast<ASTIdentifier*>($identifier), static_cast<ASTValueExpression*>($expression));
            }
            | KW_LET identifier OP_COLON type
            {
                $$ = new ASTDeclaration(@$, static_cast<ASTTypeExpression*>(static_cast<ASTTypeExpression*>($type)),
                    static_cast<ASTIdentifier*>($identifier), nullptr);
            }
            ;

if_statement : KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then]
             {
                 $$ = new ASTIfStatement(@$, static_cast<ASTValueExpression*>($condition), static_cast<ASTStatements*>($then), nullptr);
             }
             | KW_IF OP_LPAREN expression[condition] OP_RPAREN code_block[then] KW_ELSE code_block[else]
             {
                 $$ = new ASTIfStatement(@$, static_cast<ASTValueExpression*>($condition), static_cast<ASTStatements*>($then), static_cast<ASTStatements*>($else));
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
    parser.parse();

    std::cout << std::endl;
    root->print(std::cout);

    Context globals;
    root->execute(globals, globals);
    std::cout << std::endl;
    for (const auto& record : globals) {
        std::cout << record.first << " = " << std::string(*record.second) << std::endl;
    }
}