grammar Zinc;

options {
	language = Cpp;
}

program: (
		statements_ += top_level_statement
		| cpp_blocks_ += cpp_block
	)* EOF;

top_level_statement:
	declaration_statement
	| type_alias
	| function_definition
	| class_definition
	| namespace_definition
	| import_statement;

statement:
	local_block
	| expr_statement
	| declaration_statement
	| if_statement
	| switch_statement
	| match_statement
	| for_statement
	| break_statement
	| continue_statement
	| return_statement
	| type_alias
	| throw_statement;

local_block: OP_LBRACE statements_ += statement* OP_RBRACE;

expr_statement: expr_ = expr OP_SEMICOLON;

declaration_statement:
	KW_STATIC? KW_LET KW_MUT? identifier_ = any_identifier (
		OP_COLON type_ = type
	)? (OP_ASSIGN value_ = expr)? OP_SEMICOLON # LetDecl
	| (specialize_list_ = specialize_parameter_list)? KW_STATIC? KW_CONST identifier_ =
		any_identifier (
		template_list_ = template_parameter_list
		| instantiation_list_ = instantiation_list
	)? (OP_COLON type_ = type)? OP_ASSIGN value_ = expr OP_SEMICOLON # ConstDecl;

if_statement:
	KW_IF OP_LPAREN condition_ = expr OP_RPAREN if_ = local_block (
		KW_ELSE (else_ = local_block | elseif_ = if_statement)
	)?;

switch_statement:
	KW_SWITCH OP_LPAREN condition_ = expr OP_RPAREN OP_LBRACE (
		cases_ += switch_case
	)* OP_RBRACE;

switch_case:
	KW_CASE value_ = expr OP_COLON body_ = local_block
	| KW_DEFAULT OP_COLON body_ = local_block;

match_statement:
	KW_MATCH OP_LPAREN condition_ = expr OP_RPAREN OP_LBRACE (
		cases_ += match_case
	)* OP_RBRACE;

match_case: (identifier_ = T_IDENTIFIER)? OP_COLON type_ = type OP_LAMBDA body_ = local_block #
		NormalMatchCase
	| identifier_ = T_IDENTIFIER OP_LAMBDA body_ = local_block # DefaultMatchCase;

for_statement:
	KW_FOR OP_LPAREN (
		init_decl_ = declaration_statement
		| init_expr_ = expr_statement OP_SEMICOLON
		| OP_SEMICOLON
	) condition_ = expr? OP_SEMICOLON update_ = expr? OP_RPAREN body_ = local_block	# CStyleFor
	| KW_FOR OP_LPAREN condition_ = expr OP_RPAREN body_ = local_block				# WhileStyleFor;

break_statement: KW_BREAK OP_SEMICOLON;

continue_statement: KW_CONTINUE OP_SEMICOLON;

return_statement: KW_RETURN expr_ = expr? OP_SEMICOLON;

type_alias:
	KW_TYPE identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
	)? OP_ASSIGN type_ = type OP_SEMICOLON;

function_definition:
	KW_STATIC? KW_CONST? KW_VIRTUAL? KW_FUNC identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
	)? OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (OP_ARROW return_type_ = type)? (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

operator_overload_definition:
	KW_CONST? KW_OPERATOR (
		operator_ = OP_ADD
		| operator_ = OP_SUB
		| operator_ = OP_MUL
		| operator_ = OP_DIV
		| operator_ = OP_REM
		| operator_ = OP_INC
		| operator_ = OP_DEC
		| operator_ = OP_EQ
		| operator_ = OP_NEQ
		| operator_ = OP_LT
		| operator_ = OP_LTE
		| operator_ = OP_GT
		| operator_ = OP_GTE
		| operator_ = OP_AND
		| operator_ = OP_OR
		| operator_ = OP_NOT
		| operator_ = OP_BITAND
		| operator_ = OP_BITOR
		| operator_ = OP_BITXOR
		| operator_ = OP_BITNOT
		| operator_ = OP_LT op2_ = OP_LT
		| operator_ = OP_GT op2_ = OP_GT
		| operator_ = OP_ASSIGN
		| operator_ = OP_ADD_ASSIGN
		| operator_ = OP_SUB_ASSIGN
		| operator_ = OP_MUL_ASSIGN
		| operator_ = OP_DIV_ASSIGN
		| operator_ = OP_REM_ASSIGN
		| operator_ = OP_AND_ASSIGN
		| operator_ = OP_OR_ASSIGN
		| operator_ = OP_BITAND_ASSIGN
		| operator_ = OP_BITOR_ASSIGN
		| operator_ = OP_BITXOR_ASSIGN
		| operator_ = OP_LT op2_ = OP_LT op3_ = OP_ASSIGN
		| operator_ = OP_GT op2_ = OP_GT op3_ = OP_ASSIGN
		| operator_ = OP_LPAREN OP_RPAREN
		| operator_ = OP_LBRACKET OP_RBRACKET
		| operator_ = OP_ARROW
	) (template_list_ = template_parameter_list)? OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (OP_ARROW return_type_ = type)? (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

parameter:
	KW_SELF OP_COLON type_ = type															# SelfParam
	| KW_MUT? identifier_ = T_IDENTIFIER OP_COLON type_ = type								# NormalParam
	| KW_MUT? identifier_ = T_IDENTIFIER OP_COLON type_ = type OP_ASSIGN default_ = expr	#
		DefaultParam
	| KW_MUT? identifier_ = T_IDENTIFIER OP_COLON type_ = type OP_ELLIPSIS # VariadicParam;

class_definition:
	(specialize_list_ = specialize_parameter_list)? KW_CLASS identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
		| instantiation_list_ = instantiation_list
	)? (KW_EXTENDS extends_ = type)? (
		KW_IMPLEMENTS implements_ += type (
			OP_COMMA implements_ += type
		)*
	)? OP_LBRACE (
		aliases_ += type_alias
		| classes_ += class_definition
		| fields_ += declaration_statement
		| constructors_ += constructor
		| destructors_ += destructor
		| functions_ += function_definition
		| operators_ += operator_overload_definition
	)* OP_RBRACE;

namespace_definition:
	KW_NAMESPACE (identifier_ = T_IDENTIFIER)? OP_LBRACE items_ += top_level_statement* OP_RBRACE;

throw_statement: KW_THROW expr_ = expr OP_SEMICOLON;

import_statement:
	KW_IMPORT path_ = T_STRING KW_AS identifier_ = T_IDENTIFIER OP_SEMICOLON;

expr:
	KW_SELF														# SelfExpr
	| KW_SELF_TYPE												# SelfTypeExpr
	| constant_ = constant										# ConstExpr
	| identifier_ = T_IDENTIFIER								# IdentifierExpr
	| template_ = expr instantiation_list_ = instantiation_list	# InstantiationExpr
	| base_ = expr OP_DOT member_ = any_identifier				# MemberAccessExpr
	| base_ = expr OP_ARROW member_ = any_identifier			# PointerAccessExpr
	| base_ = expr OP_LBRACKET length_ = expr OP_RBRACKET		# IndexAccessExpr
	| func_ = expr OP_LPAREN (
		arguments_ += expr (OP_COMMA arguments_ += expr)*
	)? OP_RPAREN # CallExpr
	| (struct_ = type)? OP_LBRACE (
		inits_ += field_init (OP_COMMA inits_ += field_init)* OP_COMMA?
	)? OP_RBRACE # StructInitExpr
	| OP_LBRACKET (
		elements_ += expr (OP_COMMA elements_ += expr)*
	)? OP_RBRACKET																# ArrayInitExpr
	| base_ = expr OP_LBRACKET start_ = expr OP_COLON end_ = expr OP_RBRACKET	# SliceExpr
	| KW_MOVE inner_expr_ = expr												# MoveExpr
	| KW_FORWARD inner_expr_ = expr												# ForwardExpr
	| OP_LPAREN inner_expr_ = expr OP_RPAREN									# ParenExpr
	| expr_ = expr KW_AS OP_QUESTION? type_ = type								# AsExpr
	| OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (OP_COLON return_type_ = type)? OP_LAMBDA (
		expr_ = expr
		| body_ = local_block
	) # LambdaExpr
	| <assoc = right> (
		op_ = OP_INC
		| op_ = OP_DEC
		| op_ = OP_SUB
		| op_ = OP_NOT
		| op_ = OP_BITNOT
		| op_ = OP_BITAND
		| op_ = OP_MUL
	) expr_ = expr											# UnaryExpr
	| <assoc = right> expr_ = expr op_ = (OP_INC | OP_DEC)	# PostfixUnaryExpr
	| <assoc = left> left_ = expr op_ = (
		OP_MUL
		| OP_DIV
		| OP_REM
	) right_ = expr														# MultiplicativeExpr
	| <assoc = left> left_ = expr op_ = (OP_ADD | OP_SUB) right_ = expr	# AdditiveExpr
	| <assoc = left> left_ = expr (
		op_ = OP_LT OP_LT
		| op_ = OP_GT OP_GT
	) right_ = expr # ShiftExpr
	| <assoc = left> left_ = expr op_ = (
		OP_LT
		| OP_LTE
		| OP_GT
		| OP_GTE
	) right_ = expr																					# RelationalExpr
	| <assoc = left> left_ = expr op_ = (OP_EQ | OP_NEQ) right_ = expr								# EqualityExpr
	| <assoc = left> left_ = expr op_ = OP_BITAND right_ = expr										# BitAndExpr
	| <assoc = left> left_ = expr op_ = OP_BITXOR right_ = expr										# BitXorExpr
	| <assoc = left> left_ = expr op_ = OP_BITOR right_ = expr										# BitOrExpr
	| <assoc = left> left_ = expr op_ = OP_AND right_ = expr										# AndExpr
	| <assoc = left> left_ = expr op_ = OP_OR right_ = expr											# OrExpr
	| <assoc = right> condition_ = expr OP_QUESTION true_expr_ = expr OP_COLON false_expr_ = expr	#
		TernaryExpr
	| <assoc = right> left_ = expr (
		op_ = OP_ASSIGN
		| op_ = OP_ADD_ASSIGN
		| op_ = OP_SUB_ASSIGN
		| op_ = OP_MUL_ASSIGN
		| op_ = OP_DIV_ASSIGN
		| op_ = OP_REM_ASSIGN
		| op_ = OP_AND_ASSIGN
		| op_ = OP_OR_ASSIGN
		| op_ = OP_BITAND_ASSIGN
		| op_ = OP_BITOR_ASSIGN
		| op_ = OP_BITXOR_ASSIGN
		| op_ = OP_LT OP_LT OP_ASSIGN
		| op_ = OP_GT OP_GT OP_ASSIGN
	) right_ = expr # AssignExpr;

any_identifier:
	T_IDENTIFIER
	| KW_CLASS
	| KW_FUNC
	| KW_NAMESPACE
	| KW_TYPE;

constant:
	value_ = (
		T_INT
		| T_FLOAT
		| T_STRING
		| T_CHAR
		| KW_TRUE
		| KW_FALSE
		| KW_NULLPTR
	);

type:
	KW_SELF_TYPE # SelfType
	| primitive_ = (
		KW_VOID
		| KW_INT8
		| KW_INT16
		| KW_INT32
		| KW_INT64
		| KW_ISIZE
		| KW_UINT8
		| KW_UINT16
		| KW_UINT32
		| KW_UINT64
		| KW_USIZE
		| KW_FLOAT32
		| KW_FLOAT64
		| KW_BOOL
		| KW_STRVIEW
	)															# PrimitiveType
	| identifier_ = T_IDENTIFIER								# IdentifierType
	| template_ = type instantiation_list_ = instantiation_list	# InstantiatedType
	| base_ = type OP_DOT member_ = any_identifier				# MemberAccessType
	| OP_LBRACE (
		fields_ += field_decl (OP_COMMA fields_ += field_decl)* OP_COMMA?
	)? OP_RBRACE # StructType
	| OP_LBRACKET element_type_ = type (
		OP_SEMICOLON length_ = expr
	)? OP_RBRACKET # ArrayType
	| OP_LPAREN (
		parameters_ += type (OP_COMMA parameters_ += type)*
	)? OP_RPAREN OP_ARROW return_type_ = type		# FunctionType
	| KW_MOVE? OP_BITAND KW_MUT? inner_type_ = type	# ReferenceType
	| OP_MUL KW_MUT? inner_type_ = type				# PointerType
	| left_ = type OP_BITOR right_ = type			# UnionType
	| OP_LBRACKET inner_type_ = type OP_RBRACKET	# ParenType;

field_decl: identifier_ = any_identifier OP_COLON type_ = type;

field_init: identifier_ = any_identifier OP_COLON value_ = expr;

constructor:
	KW_CONST? KW_INIT (template_list_ = template_parameter_list)? OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

destructor:
	KW_CONST? KW_DROP OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

template_parameter_list:
	OP_LT parameters_ += template_parameter (
		OP_COMMA parameters_ += template_parameter
	)* OP_GT;

specialize_parameter_list:
	KW_SPECIALIZE (
		OP_LT parameters_ += template_parameter (
			OP_COMMA parameters_ += template_parameter
		)* OP_GT
	)?;

template_parameter:
	identifier_ = T_IDENTIFIER OP_ELLIPSIS? OP_COLON KW_TYPE (
		OP_EQ default_ = type
	)? # TypeTemplateParam
	| identifier_ = T_IDENTIFIER OP_ELLIPSIS? OP_COLON type_ = type (
		OP_EQ default_ = expr
	)? # ComptimeTemplateParam;

instantiation_list:
	(OP_LT | OP_TURBO_FISH) arguments_ += instantiation_argument (
		OP_COMMA arguments_ += instantiation_argument
	)* OP_GT;

instantiation_argument: type_ = type | value_ = expr;

cpp_block: T_CPP_BLOCK;

KW_LET: 'let';
KW_MUT: 'mut';
KW_CONST: 'const';
KW_VOID: 'void';
KW_INT8: 'i8';
KW_INT16: 'i16';
KW_INT32: 'i32';
KW_INT64: 'i64';
KW_ISIZE: 'isize';
KW_UINT8: 'u8';
KW_UINT16: 'u16';
KW_UINT32: 'u32';
KW_UINT64: 'u64';
KW_USIZE: 'usize';
KW_FLOAT32: 'f32';
KW_FLOAT64: 'f64';
KW_BOOL: 'bool';
KW_TRUE: 'true';
KW_FALSE: 'false';
KW_NULLPTR: 'nullptr';
KW_STRVIEW: 'strview';
KW_IF: 'if';
KW_ELSE: 'else';
KW_SWITCH: 'switch';
KW_CASE: 'case';
KW_MATCH: 'match';
KW_DEFAULT: 'default';
KW_FOR: 'for';
KW_FUNC: 'fn';
KW_BREAK: 'break';
KW_CONTINUE: 'continue';
KW_RETURN: 'return';
KW_TYPE: 'type';
KW_CLASS: 'class';
KW_EXTENDS: 'extends';
KW_IMPLEMENTS: 'implements';
KW_SELF: 'self';
KW_SELF_TYPE: 'Self';
KW_INIT: 'init';
KW_DROP: 'drop';
KW_STATIC: 'static';
KW_NAMESPACE: 'namespace';
KW_MOVE: 'move';
KW_FORWARD: 'forward';
KW_SPECIALIZE: 'specialize';
KW_OPERATOR: 'operator';
KW_AS: 'as';
KW_IMPORT: 'import';
KW_THROW: 'throw';
KW_VIRTUAL: 'virtual';

OP_DOT: '.';
OP_QUESTION: '?';
OP_COLON: ':';
OP_SEMICOLON: ';';
OP_COMMA: ',';
OP_ARROW: '->';
OP_LAMBDA: '=>';
OP_SCOPE: '::';
OP_TURBO_FISH: '::<';
OP_ELLIPSIS: '...';

OP_LPAREN: '(';
OP_RPAREN: ')';
OP_LBRACKET: '[';
OP_RBRACKET: ']';
OP_LBRACE: '{';
OP_RBRACE: '}';

OP_ADD: '+';
OP_SUB: '-';
OP_MUL: '*';
OP_DIV: '/';
OP_REM: '%';
OP_INC: '++';
OP_DEC: '--';

OP_EQ: '==';
OP_NEQ: '!=';
OP_LT: '<';
OP_LTE: '<=';
OP_GT: '>';
OP_GTE: '>=';
OP_SPACESHIP: '<=>';

OP_AND: '&&';
OP_OR: '||';
OP_NOT: '!';

OP_BITAND: '&';
OP_BITOR: '|';
OP_BITXOR: '^';
OP_BITNOT: '~';

OP_ASSIGN: '=';
OP_ADD_ASSIGN: '+=';
OP_SUB_ASSIGN: '-=';
OP_MUL_ASSIGN: '*=';
OP_DIV_ASSIGN: '/=';
OP_REM_ASSIGN: '%=';
OP_AND_ASSIGN: '&&=';
OP_OR_ASSIGN: '||=';
OP_BITAND_ASSIGN: '&=';
OP_BITOR_ASSIGN: '|=';
OP_BITXOR_ASSIGN: '^=';

T_FLOAT: T_DEC_FLOAT T_FLOAT_SUFFIX?;

fragment T_DEC_FLOAT:
	[0-9]+ '.' [0-9]* EXP?
	| '.' [0-9]+ EXP?
	| [0-9]+ EXP;

fragment EXP: [eE] [+-]? [0-9]+;

fragment T_FLOAT_SUFFIX: 'f32' | 'f64';

T_INT:
	(
		'0' [xX] [0-9a-fA-F]+
		| '0' [bB] [0-1]+
		| '0' [oO] [0-7]+
		| [0-9]+
	) T_INT_SUFFIX?;

fragment T_INT_SUFFIX:
	'i8'
	| 'i16'
	| 'i32'
	| 'i64'
	| 'isize'
	| 'u8'
	| 'u16'
	| 'u32'
	| 'u64'
	| 'usize';

T_STRING: '"' (~["\r\n] | '\\' .)* '"';
T_CHAR: '\'' (~['\r\n] | '\\' .) '\'';
T_IDENTIFIER: [a-zA-Z_][a-zA-Z0-9_]*;

WHITESPACE: [ \t\n\r\f]+ -> skip;

LINE_COMMENT: '//' ~[\r\n]* -> channel(HIDDEN);

BLOCK_COMMENT: '/*' .*? '*/' -> channel(HIDDEN);

T_CPP_BLOCK:
	'#CPP' [ \t]* '{' [ \t]* '\r'? '\n' .*? '\r'? '\n' '}';