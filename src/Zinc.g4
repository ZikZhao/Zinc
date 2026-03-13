grammar Zinc;

options {
	language = Cpp;
}

@parser::members {
    bool isInstantiationList() {
        // 如果当前面对的根本不是 '<'，直接返回 false
        if (_input->LA(1) != ZincParser::OP_LT) {
            return false;
        }

        int i = 1; 
        int depth_paren = 0;   // () 深度
        int depth_bracket = 0; // [] 深度
        int depth_brace = 0;   // {} 深度
        int depth_angle = 0;   // <> 深度

        while (true) {
            int type = _input->LA(i);
            
            // 遇到文件结束，或语句级分隔符，说明匹配失败
            if (type == antlr4::Token::EOF || type == ZincParser::OP_SEMICOLON || type == ZincParser::OP_LBRACE) {
                return false;
            }

            if (type == ZincParser::OP_LPAREN) depth_paren++;
            else if (type == ZincParser::OP_RPAREN) depth_paren--;
            else if (type == ZincParser::OP_LBRACKET) depth_bracket++;
            else if (type == ZincParser::OP_RBRACKET) depth_bracket--;
            
            // 只有在普通的括号没有发生嵌套时，才处理尖括号
            else if (depth_paren == 0 && depth_bracket == 0 && depth_brace == 0) {
                if (type == ZincParser::OP_LT) {
                    depth_angle++;
                } else if (type == ZincParser::OP_GT) {
                    depth_angle--;
                    if (depth_angle == 0) {
                        return true; // 找到了完美闭合的最外层 '>'
                    }
                }
            }
            i++;
        }
    }
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
		OP_COLON type_ = expr
	)? (OP_ASSIGN value_ = expr)? OP_SEMICOLON # LetDecl
	| (specialize_list_ = specialize_parameter_list)? KW_CONST identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
		| instantiation_list_ = instantiation_list
	)? (OP_COLON type_ = expr)? OP_ASSIGN value_ = expr OP_SEMICOLON # ConstDecl;

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
	)? OP_ASSIGN type_ = expr OP_SEMICOLON;

function_definition:
	KW_CONST? KW_STATIC? KW_FUNC identifier_ = T_IDENTIFIER (
		template_list_ = template_parameter_list
	)? OP_LPAREN (
		parameters_ += parameter (
			OP_COMMA parameters_ += parameter
		)*
	)? OP_RPAREN (OP_ARROW return_type_ = expr)? (
		OP_LBRACE body_ += statement* OP_RBRACE
		| semi_ = OP_SEMICOLON
	);

parameter:
	KW_SELF OP_COLON type_ = expr						# SelfParam
	| identifier_ = T_IDENTIFIER OP_COLON type_ = expr	# NormalParam;

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
	| KW_SELF_TYPE				# SelfTypeExpr
	| constant_ = constant		# ConstExpr
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
	) # PrimitiveTypeExpr
	| identifier_ = identifier	# IdentifierExpr
	| base_ = expr (OP_DOT members_ += T_IDENTIFIER)+ (
		instantiation_list_ = instantiation_list
	)? # AccessChainExpr
	| base_ = expr (OP_DOT members_ += T_IDENTIFIER)* { isInstantiationList() }? 
		instantiation_list_ = instantiation_list # AccessChainExprAlternative
	| func_ = expr OP_LPAREN (
		arguments_ += expr (OP_COMMA arguments_ += expr)*
	)? OP_RPAREN							# CallExpr
	| OP_BITAND KW_MUT? inner_expr_ = expr	# AddressOfExpr
	| OP_LBRACE (
		inits_ += field_init (OP_COMMA inits_ += field_init)* OP_COMMA?
	)? OP_RBRACE # AnonymousStructInitExpr
	| struct_ = expr OP_LBRACE (
		inits_ += field_init (OP_COMMA inits_ += field_init)* OP_COMMA?
	)? OP_RBRACE								# StructInitExpr
	| OP_LPAREN inner_expr_ = expr OP_RPAREN	# ParenExpr
	| OP_LBRACE (
		fields_ += field_decl (OP_COMMA fields_ += field_decl)* OP_COMMA?
	)? OP_RBRACE # StructTypeExpr
	| OP_LPAREN (
		parameters_ += expr (OP_COMMA parameters_ += expr)*
	)? OP_RPAREN OP_ARROW return_type_ = expr		# FunctionTypeExpr
	| KW_MUT inner_type_ = expr						# MutableTypeExpr
	| KW_MOVE? OP_BITAND inner_type_ = expr			# ReferenceTypeExpr
	| OP_MUL inner_type_ = expr						# PointerTypeExpr
	| inner_type_ = expr OP_QUESTION				# OptionalTypeExpr
	| <assoc = right> op_ = (
		OP_INC
		| OP_DEC
		| OP_SUB
		| OP_NOT
		| OP_BITNOT
	) expr_ = expr				# UnaryExpr
    | <assoc = left> left_ = expr op_ = (
		OP_MUL
		| OP_DIV
		| OP_REM
	) right_ = expr # MultiplicativeExpr
	| <assoc = left> left_ = expr op_ = (OP_ADD | OP_SUB) right_ = expr			# AdditiveExpr
	| <assoc = left> left_ = expr op_ = (OP_LSHIFT | OP_RSHIFT) right_ = expr	# ShiftExpr
	| <assoc = left> left_ = expr op_ = (
		OP_LT
		| OP_LTE
		| OP_GT
		| OP_GTE
	) right_ = expr																# RelationalExpr
	| <assoc = left> left_ = expr op_ = (OP_EQ | OP_NEQ) right_ = expr	# EqualityExpr
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
	) right_ = expr														# AssignExpr;

identifier: name_ = T_IDENTIFIER;

constant:
	value_ = (T_INT | T_FLOAT | T_STRING | T_BOOL | KW_NULLPTR);

field_decl: identifier_ = T_IDENTIFIER OP_COLON type_ = expr;

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
		OP_EQ default_ = expr
	)? # TypeTemplateParam
	| identifier_ = T_IDENTIFIER OP_COLON type_ = expr (
		OP_EQ default_ = expr
	) # ComptimeTemplateParam;

instantiation_list:
	OP_LT arguments_ += expr (
		OP_COMMA arguments_ += expr
	)* OP_GT;

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