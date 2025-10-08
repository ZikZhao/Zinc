%{
#include "pch.hpp"
#include "ast.hpp"
#include "out/parser.tab.hpp"

using namespace yy;
using enum parser::token::token_kind_type;

extern std::ifstream yyin;
uint64_t column = 0;
%}

%option c++
%option noyywrap
%option yylineno

DIGIT [0-9]
LETTER [a-zA-Z_]
IDENTIFIER {LETTER}({LETTER}|{DIGIT})*
INTEGER {DIGIT}+
FLOAT {DIGIT}+\.{DIGIT}+
STRING \"([^"\\]|\\.)*\"
CHAR \'([^'\\]|\\.)*\'

WHITESPACE [ \t]+
NEW_LINE [\n\r]

SINGLE_LINE_COMMENT \/\/.*
MULTI_LINE_COMMENT \/\*([^*]|\*+[^*/])*\*+\/

%%

"const"                  { return KW_CONST; }
"let"                    { return KW_LET; }
"integer"                { return KW_INTEGER; }
"float"                  { return KW_FLOAT; }
"string"                 { return KW_STRING; }
"boolean"                { return KW_BOOLEAN; }
"null"                   { return KW_NULL; }
"if"                     { return KW_IF; }
"else"                   { return KW_ELSE; }
"for"                    { return KW_FOR; }
"while"                  { return KW_WHILE; }
"function"               { return KW_FUNCTION; }
"return"                 { return KW_RETURN; }
"class"                  { return KW_CLASS; }
"extends"                { return KW_EXTENDS; }
"break"                  { return KW_BREAK; }
"continue"               { return KW_CONTINUE; }
"switch"                 { return KW_SWITCH; }
"case"                   { return KW_CASE; }
"default"                { return KW_DEFAULT; }
"import"                 { return KW_IMPORT; }
"from"                   { return KW_FROM; }
"as"                     { return KW_AS; }

"+"                      { return OP_ADD; }
"-"                      { return OP_SUBTRACT; }
"*"                      { return OP_MULTIPLY; }
"/"                      { return OP_DIVIDE; }
"%"                      { return OP_REMAINDER; }
"++"                     { return OP_INCREMENT; }
"--"                     { return OP_DECREMENT; }

"=="                     { return OP_EQUAL; }
"!="                     { return OP_NOT_EQUAL; }
"<"                      { return OP_LESS_THAN; }
">"                      { return OP_GREATER_THAN; }
"<="                     { return OP_LESS_EQUAL; }
">="                     { return OP_GREATER_EQUAL; }

"&&"                     { return OP_LOGICAL_AND; }
"||"                     { return OP_LOGICAL_OR; }
"!"                      { return OP_LOGICAL_NOT; }

"&"                      { return OP_BITWISE_AND; }
"|"                      { return OP_BITWISE_OR; }
"^"                      { return OP_BITWISE_XOR; }
"~"                      { return OP_BITWISE_NOT; }
"<<"                     { return OP_LEFT_SHIFT; }
">>"                     { return OP_RIGHT_SHIFT; }
">>>"                    { return OP_UNSIGNED_RIGHT_SHIFT; }

"="                      { return OP_ASSIGN; }
"+="                     { return OP_ADD_ASSIGN; }
"-="                     { return OP_SUBTRACT_ASSIGN; }
"*="                     { return OP_MULTIPLY_ASSIGN; }
"/="                     { return OP_DIVIDE_ASSIGN; }
"%="                     { return OP_REMAINDER_ASSIGN; }

"?"                      { return OP_QUESTION; }
":"                      { return OP_COLON; }
"=>"                     { return OP_ARROW; }

";"                      { return OP_SEMICOLON; }
","                      { return OP_COMMA; }
"."                      { return OP_DOT; }

"("                      { return OP_LPAREN; }
")"                      { return OP_RPAREN; }
"["                      { return OP_LBRACKET; }
"]"                      { return OP_RBRACKET; }
"{"                      { return OP_LBRACE; }
"}"                      { return OP_RBRACE; }

"`"                      { return OP_TEMPLATE_STRING; }

{INTEGER}                { return T_INTEGER; }
{FLOAT}                  { return T_FLOAT; }
{STRING}                 { return T_STRING; }

{IDENTIFIER}             { return T_IDENTIFIER; }

{SINGLE_LINE_COMMENT}    { return T_SINGLE_LINE_COMMENT; }
{MULTI_LINE_COMMENT}     { return T_MULTI_LINE_COMMENT; }

{WHITESPACE}             { /* Ignore whitespace */ }
{NEW_LINE}               { column = 0; std::cout << std::endl; }

.                        { printf("UNKNOWN(%c) ", yytext[0]); }

%%
extern std::ifstream yyin;

int yylex(ASTNode** yylval, Location* yylloc) {
    static yyFlexLexer lexer(&yyin, nullptr);
    static uint64_t column = 0;

    yylloc->begin.line = lexer.lineno();
    yylloc->begin.column = column;
    const int type = lexer.yylex();
    column += strlen(lexer.YYText());
    yylloc->end.line = lexer.lineno();
    yylloc->end.column = column;
    *yylval = ASTToken::New(*yylloc, lexer.YYText(), type);
    return type;
}
