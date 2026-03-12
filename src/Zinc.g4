grammar Zinc;

options {
	language = Cpp;
}

program: statements_ += top_level_statement* EOF;

top_level_statement:
	declaration_statement
	| type_alias
	| function_definition
	| class_definition
	| namespace_definition;

statement:
	local_block
	| expr_statement
	| declaration_statement
	| if_statement
	| for_statement
	| break_statement
	| continue_statement
	| return_statement
	| type_alias
	| function_definition
	| class_definition;

namespace_item:
	declaration_statement
	| type_alias
	| function_definition
	| class_definition
	| namespace_definition;

local_block: OP_LBRACE statements_ += statement* OP_RBRACE;

expr_statement: expr_ = expr OP_SEMICOLON;

declaration_statement:
	KW_LET identifier_ = T_IDENTIFIER KW_MUT? (
		OP_COLON type_ = type
	)? (OP_ASSIGN value_ = expr)? OP_SEMICOLON # LetDecl
	| (specialize_list_ = specialize_parameter_list)? KW_CONST identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
		| instantiation_list_ = instantiation_list
	)? (OP_COLON type_ = type)? OP_ASSIGN value_ = expr OP_SEMICOLON # ConstDecl;

if_statement:
	KW_IF OP_LPAREN condition_ = expr OP_RPAREN if_ = local_block (
		KW_ELSE else_ = local_block
	)?;

for_statement:
	KW_FOR OP_LPAREN (
		init_decl_ = declaration_statement
		| init_expr_ = expr_statement
		| OP_SEMICOLON
	) condition_ = expr? OP_SEMICOLON update_ = expr? OP_RPAREN body_ = local_block;

break_statement: KW_BREAK OP_SEMICOLON;

continue_statement: KW_CONTINUE OP_SEMICOLON;

return_statement: KW_RETURN expr_ = expr? OP_SEMICOLON;

type_alias:
	KW_TYPE identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
	)? OP_ASSIGN type_ = type OP_SEMICOLON;

function_definition:
	KW_CONST? KW_STATIC? KW_FUNC identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
	)? OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (OP_ARROW return_type_ = type)? (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

parameter:
	KW_SELF OP_COLON type_ = type						# SelfParam
	| identifier_ = T_IDENTIFIER OP_COLON type_ = type	# NormalParam;

class_definition:
	(specialize_list_ = specialize_parameter_list)? KW_CLASS identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
		| instantiation_list_ = instantiation_list
	)? (KW_EXTENDS extends_ = identifier)? (
		KW_IMPLEMENTS implements_ += identifier (
			OP_COMMA implements_ += identifier
		)*
	)? OP_LBRACE (
		constructor_ += constructor
		| destructor_ += destructor
		| fields_ += declaration_statement
		| types_ += type_alias
		| functions_ += function_definition
		| classes_ += class_definition
	)* OP_RBRACE;

namespace_definition:
	KW_NAMESPACE (identifier_ = T_IDENTIFIER)? OP_LBRACE items_ += namespace_item* OP_RBRACE;

expr:
	KW_SELF # SelfExpr
	| <assoc = right> left_ = expr op_ = (
		OP_ASSIGN
		| OP_ADD_ASSIGN
		| OP_SUB_ASSIGN
		| OP_MUL_ASSIGN
		| OP_DIV_ASSIGN
		| OP_REM_ASSIGN
		| OP_AND_ASSIGN
		| OP_OR_ASSIGN
		| OP_BITAND_ASSIGN
		| OP_BITOR_ASSIGN
		| OP_BITXOR_ASSIGN
		| OP_LSHIFT_ASSIGN
		| OP_RSHIFT_ASSIGN
	) right_ = expr														# AssignExpr
	| <assoc = left> left_ = expr op_ = (OP_EQ | OP_NEQ) right_ = expr	# EqualityExpr
	| <assoc = left> left_ = expr op_ = (
		OP_LT
		| OP_LTE
		| OP_GT
		| OP_GTE
	) right_ = expr																# RelationalExpr
	| <assoc = left> left_ = expr op_ = (OP_LSHIFT | OP_RSHIFT) right_ = expr	# ShiftExpr
	| <assoc = left> left_ = expr op_ = (OP_ADD | OP_SUB) right_ = expr			# AdditiveExpr
	| <assoc = left> left_ = expr op_ = (
		OP_MUL
		| OP_DIV
		| OP_REM
	) right_ = expr # MultiplicativeExpr
	| <assoc = right> op_ = (
		OP_INC
		| OP_DEC
		| OP_SUB
		| OP_NOT
		| OP_BITNOT
	) expr_ = expr														# UnaryExpr
	| constant_ = constant												# ConstExpr
	| identifier_ = identifier											# IdentifierExpr
	| identifier_ = T_IDENTIFIER template_args_ = instantiation_list	# TemplateIdentifierExpr
	| func_ = expr OP_LPAREN (
		arguments_ += expr (OP_COMMA arguments_ += expr)*
	)? OP_RPAREN																		# CallExpr
	| OP_BITAND KW_MUT? inner_expr_ = expr												# AddressOfExpr
	| target_ = expr (OP_DOT members_ += identifier)+									# MemberAccessExpr
	| target_ = expr OP_DOT member_ = identifier template_args_ = instantiation_list	#
		TemplateMemberAccessExpr
	| (struct_ = type)? OP_LBRACE (
		inits_ += field_init (OP_COMMA inits_ += field_init)* OP_COMMA?
	)? OP_RBRACE								# StructInitExpr
	| OP_LPAREN inner_expr_ = expr OP_RPAREN	# ParenExpr;

identifier: name_ = T_IDENTIFIER;

constant:
	value_ = (T_INT | T_FLOAT | T_STRING | T_BOOL | KW_NULLPTR);

type:
	KW_SELF_TYPE # SelfType
	| primitive_ = (
		KW_INT8
		| KW_INT16
		| KW_INT32
		| KW_INT64
		| KW_UINT8
		| KW_UINT16
		| KW_UINT32
		| KW_UINT64
		| KW_FLOAT32
		| KW_FLOAT64
		| KW_STRING
		| KW_BOOL
	)																# PrimitiveType
	| identifier_ = identifier										# IdentifierType
	| identifier_ = identifier template_args_ = instantiation_list	# TemplateTypeInstantiation
	| OP_LBRACE (
		fields_ += field_decl (OP_COMMA fields_ += field_decl)* OP_COMMA?
	)? OP_RBRACE # StructType
	| OP_LPAREN (
		parameters_ += type (OP_COMMA parameters_ += type)*
	)? OP_RPAREN OP_ARROW return_type_ = type		# FunctionType
	| KW_MUT inner_type_ = type						# MutableType
	| KW_MOVE? OP_BITAND inner_type_ = type			# ReferenceType
	| OP_MUL inner_type_ = type						# PointerType
	| inner_type_ = type OP_QUESTION				# OptionalType
	| OP_LBRACKET inner_type_ = type OP_RBRACKET	# ParenType;

field_decl: identifier_ = T_IDENTIFIER OP_COLON type_ = type;

field_init: identifier_ = T_IDENTIFIER OP_COLON value_ = expr;

constructor:
	KW_INIT OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN OP_LBRACE body_ += statement* OP_RBRACE;

destructor:
	KW_DROP OP_LPAREN OP_RPAREN OP_LBRACE body_ += statement* OP_RBRACE;

template_parameter_list:
	OP_LT parameters_ += template_parameter (
		OP_COMMA parameters_ += template_parameter
	)* OP_GT;

specialize_parameter_list:
	KW_SPECIALIZE (
		OP_LT (OP_COMMA parameters_ += template_parameter)+ OP_GT
	)?;

template_parameter:
	identifier_ = T_IDENTIFIER OP_COLON KW_TYPE (
		OP_EQ default_ = type
	)? # TypeTemplateParam
	| identifier_ = T_IDENTIFIER OP_COLON type_ = type (
		OP_EQ default_ = expr
	) # ComptimeTemplateParam;

instantiation_list:
	OP_LT arguments_ += instantiation_argument (
		OP_COMMA arguments_ += instantiation_argument
	)* OP_GT;

instantiation_argument: type_ = type | value_ = expr;

KW_LET: 'let';
KW_MUT: 'mut';
KW_CONST: 'const';
KW_INT8: 'i8';
KW_INT16: 'i16';
KW_INT32: 'i32';
KW_INT64: 'i64';
KW_UINT8: 'u8';
KW_UINT16: 'u16';
KW_UINT32: 'u32';
KW_UINT64: 'u64';
KW_FLOAT32: 'f32';
KW_FLOAT64: 'f64';
KW_STRING: 'string';
KW_BOOL: 'bool';
KW_NULLPTR: 'nullptr';
KW_IF: 'if';
KW_ELSE: 'else';
KW_SWITCH: 'switch';
KW_CASE: 'case';
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

OP_AND: '&&';
OP_OR: '||';
OP_NOT: '!';

OP_BITAND: '&';
OP_BITOR: '|';
OP_BITXOR: '^';
OP_BITNOT: '~';
OP_LSHIFT: '<<';
OP_RSHIFT: '>>';

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
OP_LSHIFT_ASSIGN: '<<=';
OP_RSHIFT_ASSIGN: '>>=';

OP_DOT: '.';
OP_QUESTION: '?';
OP_COLON: ':';
OP_SEMICOLON: ';';
OP_COMMA: ',';
OP_ARROW: '->';
OP_LAMBDA: '=>';

OP_LPAREN: '(';
OP_RPAREN: ')';
OP_LBRACKET: '[';
OP_RBRACKET: ']';
OP_LBRACE: '{';
OP_RBRACE: '}';

T_FLOAT: T_HEX_FLOAT | T_DEC_FLOAT;

fragment T_HEX_FLOAT:
	'0' [xX] [0-9a-fA-F]* '.' [0-9a-fA-F]+ [pP] [+-]? [0-9]+
	| '0' [xX] [0-9a-fA-F]+ [pP] [+-]? [0-9]+;

fragment T_DEC_FLOAT:
	[0-9]+ '.' [0-9]* EXP?
	| '.' [0-9]+ EXP?
	| [0-9]+ EXP;

fragment EXP: [eE] [+-]? [0-9]+;

T_INT:
	'0' [xX] [0-9a-fA-F]+
	| '0' [bB] [0-1]+
	| '0' [0-7]+
	| [0-9]+;

T_STRING: '"' (~["\r\n] | '\\' .)* '"';
T_BOOL: 'true' | 'false';
T_IDENTIFIER: [a-zA-Z_][a-zA-Z0-9_]*;

WHITESPACE: [ \t\n\r\f]+ -> skip;

LINE_COMMENT: '//' ~[\r\n]* -> channel(HIDDEN);

BLOCK_COMMENT: '/*' .*? '*/' -> channel(HIDDEN);