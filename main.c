#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>

typedef enum {
    VAL_NONE,
    VAL_NIL,
    VAL_UNDEFINED,
    VAL_NULL,
    VAL_PAIR,
    VAL_FUNCTION,
    VAL_BUILTIN
} ValueType;

typedef struct Value Value;
typedef struct Environment Environment;
typedef struct ASTNode ASTNode;

struct Value {
    ValueType type;
    union {
        struct {
            Value* car;
            Value* cdr;
        } pair;
        struct {
            char* name;
            char** params;
            int param_count;
            ASTNode* body;
            Environment* closure;
        } function;
        struct {
            char* name;
            Value* (*func)(Value** args, int argc, Environment* env);
        } builtin;
    } data;
    int ref_count;
};

//最初のTOKENいらないだろ
typedef enum {
    TOKEN_NONE, TOKEN_NIL, TOKEN_UNDEFINED, TOKEN_NULL,
    TOKEN_FUNCTION, TOKEN_IF, TOKEN_ELSE, TOKEN_MATCH, TOKEN_CASE, TOKEN_DEFAULT,
    TOKEN_PAIR, TOKEN_LIST, TOKEN_IDENTIFIER,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_COMMA, TOKEN_ARROW, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
} Token;

typedef struct {
    char* input;
    int pos;
    int length;
    int line;
    int column;
} Lexer;

typedef enum {
    AST_VALUE, AST_IDENTIFIER, AST_PAIR, AST_LIST,
    AST_FUNCTION_CALL, AST_FUNCTION_DEF, AST_IF, AST_MATCH
} ASTType;

struct ASTNode {
    ASTType type;
    union {
        Value* value;
        char* identifier;
        struct {
            ASTNode* car;
            ASTNode* cdr;
        } pair;
        struct {
            ASTNode** elements;
            int count;
        } list;
        struct {
            ASTNode* func;
            ASTNode** args;
            int argc;
        } call;
        struct {
            char* name;
            char** params;
            int param_count;
            ASTNode* body;
        } func_def;
        struct {
            ASTNode* condition;
            ASTNode* then_branch;
            ASTNode* else_branch;
        } if_node;
        struct {
            ASTNode* value;
            ASTNode** patterns;
            ASTNode** bodies;
            int case_count;
            ASTNode* default_case;
        } match;
    } data;
};

typedef struct EnvironmentEntry {
    char* name;
    Value* value;
    struct EnvironmentEntry* next;
} EnvironmentEntry;

struct Environment {
    EnvironmentEntry* bindings;
    Environment* parent;
    int ref_count;
};

typedef struct {
    Token* tokens;
    int pos;
    int count;
} Parser;


Value* value_new(ValueType type) {
    Value* val = malloc(sizeof(Value));
    val->type = type;
    val->ref_count = 1;
    return val;
}

void value_retain(Value* val) {
    if (val) val->ref_count++;
}

void value_release(Value* val);

void value_release(Value* val) {
    if (!val || --val->ref_count > 0) return;

    switch (val->type) {
        case VAL_PAIR:
            value_release(val->data.pair.car);
            value_release(val->data.pair.cdr);
            break;
        case VAL_FUNCTION:
            free(val->data.function.name);
            for (int i = 0; i < val->data.function.param_count; i++) {
                free(val->data.function.params[i]);
            }
            free(val->data.function.params);
            break;
        case VAL_BUILTIN:
            free(val->data.builtin.name);
            break;
        default:
            break;
    }
    free(val);
}

//全部分けなくてよかったかも
Value* make_none() {
    return value_new(VAL_NONE);
}

Value* make_nil() {
    return value_new(VAL_NIL);
}

Value* make_undefined() {
    return value_new(VAL_UNDEFINED);
}

Value* make_null() {
    return value_new(VAL_NULL);
}

Value* make_pair(Value* car, Value* cdr) {
    Value* val = value_new(VAL_PAIR);
    val->data.pair.car = car;
    val->data.pair.cdr = cdr;
    value_retain(car);
    value_retain(cdr);
    return val;
}

Environment* env_new(Environment* parent) {
    Environment* env = malloc(sizeof(Environment));
    env->bindings = NULL;
    env->parent = parent;
    env->ref_count = 1;
    return env;
}

void env_retain(Environment* env) {
    if (env) env->ref_count++;
}

void env_release(Environment* env) {
    if (!env || --env->ref_count > 0) return;

    EnvironmentEntry* entry = env->bindings;
    while (entry) {
        EnvironmentEntry* next = entry->next;
        free(entry->name);
        value_release(entry->value);
        free(entry);
        entry = next;
    }
    free(env);
}

void env_define(Environment* env, const char* name, Value* value) {
    EnvironmentEntry* entry = malloc(sizeof(EnvironmentEntry));
    entry->name = strdup(name);
    entry->value = value;
    entry->next = env->bindings;
    env->bindings = entry;
    value_retain(value);
}

Value* env_lookup(Environment* env, const char* name) {
    for (EnvironmentEntry* entry = env->bindings; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return entry->value;
        }
    }
    if (env->parent) {
        return env_lookup(env->parent, name);
    }
    return NULL;
}


Lexer* lexer_new(const char* input) {
    Lexer* lexer = malloc(sizeof(Lexer));
    lexer->input = strdup(input);
    lexer->pos = 0;
    lexer->length = strlen(input);
    lexer->line = 1;
    lexer->column = 1;
    return lexer;
}

void lexer_free(Lexer* lexer) {
    free(lexer->input);
    free(lexer);
}

void skip_whitespace(Lexer* lexer) {
    while (lexer->pos < lexer->length && isspace(lexer->input[lexer->pos])) {
        if (lexer->input[lexer->pos] == '\n') {
            lexer->line++;
            lexer->column = 1;
        } else {
            lexer->column++;
        }
        lexer->pos++;
    }
}

bool match_keyword(Lexer* lexer, const char* keyword, TokenType* type) {
    int len = strlen(keyword);
    if (lexer->pos + len <= lexer->length &&
        strncmp(lexer->input + lexer->pos, keyword, len) == 0 &&
        (lexer->pos + len == lexer->length || !isalnum(lexer->input[lexer->pos + len]))) {

        if (strcmp(keyword, "none") == 0) *type = TOKEN_NONE;
        else if (strcmp(keyword, "nil") == 0) *type = TOKEN_NIL;
        else if (strcmp(keyword, "undefined") == 0) *type = TOKEN_UNDEFINED;
        else if (strcmp(keyword, "null") == 0) *type = TOKEN_NULL;
        else if (strcmp(keyword, "function") == 0) *type = TOKEN_FUNCTION;
        else if (strcmp(keyword, "if") == 0) *type = TOKEN_IF;
        else if (strcmp(keyword, "else") == 0) *type = TOKEN_ELSE;
        else if (strcmp(keyword, "match") == 0) *type = TOKEN_MATCH;
        else if (strcmp(keyword, "case") == 0) *type = TOKEN_CASE;
        else if (strcmp(keyword, "default") == 0) *type = TOKEN_DEFAULT;
        else if (strcmp(keyword, "pair") == 0) *type = TOKEN_PAIR;
        else if (strcmp(keyword, "list") == 0) *type = TOKEN_LIST;
        else return false;

        return true;
    }
    return false;
}

Token next_token(Lexer* lexer) {
    Token token = {TOKEN_EOF, NULL, lexer->line, lexer->column};

    skip_whitespace(lexer);

    if (lexer->pos >= lexer->length) {
        return token;
    }

    char c = lexer->input[lexer->pos];

    //単一文字
    switch (c) {
        case '(':
            token.type = TOKEN_LPAREN;
            token.value = strdup("(");
            lexer->pos++;
            lexer->column++;
            return token;
        case ')':
            token.type = TOKEN_RPAREN;
            token.value = strdup(")");
            lexer->pos++;
            lexer->column++;
            return token;
        case '{':
            token.type = TOKEN_LBRACE;
            token.value = strdup("{");
            lexer->pos++;
            lexer->column++;
            return token;
        case '}':
            token.type = TOKEN_RBRACE;
            token.value = strdup("}");
            lexer->pos++;
            lexer->column++;
            return token;
        case ',':
            token.type = TOKEN_COMMA;
            token.value = strdup(",");
            lexer->pos++;
            lexer->column++;
            return token;
    }

    //->
    if (c == '-' && lexer->pos + 1 < lexer->length && lexer->input[lexer->pos + 1] == '>') {
        token.type = TOKEN_ARROW;
        token.value = strdup("->");
        lexer->pos += 2;
        lexer->column += 2;
        return token;
    }

    if (isalpha(c) || c == '_') {
        int start = lexer->pos;
        while (lexer->pos < lexer->length && (isalnum(lexer->input[lexer->pos]) || lexer->input[lexer->pos] == '_')) {
            lexer->pos++;
            lexer->column++;
        }

        int len = lexer->pos - start;
        char* word = malloc(len + 1);
        strncpy(word, lexer->input + start, len);
        word[len] = '\0';

        TokenType keyword_type;
        lexer->pos = start;
        lexer->column -= len;

        if (match_keyword(lexer, word, &keyword_type)) {
            lexer->pos += len;
            lexer->column += len;
            token.type = keyword_type;
            token.value = word;
            return token;
        }

        lexer->pos += len;
        lexer->column += len;
        token.type = TOKEN_IDENTIFIER;
        token.value = word;
        return token;
    }

    printf("error: unknown character '%c' at line %d, column %d\n", c, lexer->line, lexer->column);
    exit(1);
}

Parser* parser_new(Token* tokens, int count) {
    Parser* parser = malloc(sizeof(Parser));
    parser->tokens = tokens;
    parser->pos = 0;
    parser->count = count;
    return parser;
}

void parser_free(Parser* parser) {
    free(parser);
}

Token* current_token(Parser* parser) {
    if (parser->pos < parser->count) {
        return &parser->tokens[parser->pos];
    }
    return NULL;
}

Token* consume(Parser* parser, TokenType expected) {
    Token* token = current_token(parser);
    if (!token || token->type != expected) {
        printf("error: expected:  %d, actually: %d\n", expected, token ? token->type : -1);
        exit(1);
    }
    parser->pos++;
    return token;
}

ASTNode* ast_new(ASTType type) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = type;
    return node;
}

ASTNode* parse_expression(Parser* parser);

ASTNode* parse_primary(Parser* parser) {
    Token* token = current_token(parser);
    if (!token) {
        printf("error: unexpected EOF\n");
        exit(1);
    }

    switch (token->type) {
        case TOKEN_NONE: {
            parser->pos++;
            ASTNode* node = ast_new(AST_VALUE);
            node->data.value = make_none();
            return node;
        }
        case TOKEN_NIL: {
            parser->pos++;
            ASTNode* node = ast_new(AST_VALUE);
            node->data.value = make_nil();
            return node;
        }
        case TOKEN_UNDEFINED: {
            parser->pos++;
            ASTNode* node = ast_new(AST_VALUE);
            node->data.value = make_undefined();
            return node;
        }
        case TOKEN_NULL: {
            parser->pos++;
            ASTNode* node = ast_new(AST_VALUE);
            node->data.value = make_null();
            return node;
        }
        case TOKEN_PAIR: {
            parser->pos++;
            consume(parser, TOKEN_LPAREN);
            ASTNode* car = parse_expression(parser);
            consume(parser, TOKEN_COMMA);
            ASTNode* cdr = parse_expression(parser);
            consume(parser, TOKEN_RPAREN);

            ASTNode* node = ast_new(AST_PAIR);
            node->data.pair.car = car;
            node->data.pair.cdr = cdr;
            return node;
        }
        case TOKEN_LIST: {
            parser->pos++;
            consume(parser, TOKEN_LPAREN);

            ASTNode** elements = malloc(sizeof(ASTNode*) * 100);
            int count = 0;

            while (current_token(parser) && current_token(parser)->type != TOKEN_RPAREN) {
                elements[count++] = parse_expression(parser);
                if (current_token(parser) && current_token(parser)->type == TOKEN_COMMA) {
                    parser->pos++;
                }
            }

            consume(parser, TOKEN_RPAREN);

            ASTNode* node = ast_new(AST_LIST);
            node->data.list.elements = elements;
            node->data.list.count = count;
            return node;
        }
        case TOKEN_IDENTIFIER: {
            char* name = strdup(token->value);
            parser->pos++;

            ASTNode* node = ast_new(AST_IDENTIFIER);
            node->data.identifier = name;
            return node;
        }
        case TOKEN_LPAREN: {
            parser->pos++;
            ASTNode* expr = parse_expression(parser);
            consume(parser, TOKEN_RPAREN);
            return expr;
        }
        default:
            printf("error: unexpected token %d\n", token->type);
            exit(1);
    }
}

ASTNode* parse_function_call(Parser* parser) {
    ASTNode* expr = parse_primary(parser);

    while (current_token(parser) && current_token(parser)->type == TOKEN_LPAREN) {
        parser->pos++;

        ASTNode** args = malloc(sizeof(ASTNode*) * 100);
        int argc = 0;

        while (current_token(parser) && current_token(parser)->type != TOKEN_RPAREN) {
            args[argc++] = parse_expression(parser);
            if (current_token(parser) && current_token(parser)->type == TOKEN_COMMA) {
                parser->pos++;
            }
        }

        consume(parser, TOKEN_RPAREN);

        ASTNode* call = ast_new(AST_FUNCTION_CALL);
        call->data.call.func = expr;
        call->data.call.args = args;
        call->data.call.argc = argc;
        expr = call;
    }

    return expr;
}

ASTNode* parse_match(Parser* parser) {
    if (current_token(parser) && current_token(parser)->type == TOKEN_MATCH) {
        parser->pos++;
        ASTNode* value = parse_function_call(parser);
        consume(parser, TOKEN_LBRACE);

        ASTNode** patterns = malloc(sizeof(ASTNode*) * 100);
        ASTNode** bodies = malloc(sizeof(ASTNode*) * 100);
        int case_count = 0;
        ASTNode* default_case = NULL;

        while (current_token(parser) &&
               (current_token(parser)->type == TOKEN_CASE || current_token(parser)->type == TOKEN_DEFAULT)) {
            if (current_token(parser)->type == TOKEN_CASE) {
                parser->pos++;
                patterns[case_count] = parse_function_call(parser);
                consume(parser, TOKEN_ARROW);
                bodies[case_count] = parse_expression(parser);
                case_count++;
            } else if (current_token(parser)->type == TOKEN_DEFAULT) {
                parser->pos++;
                consume(parser, TOKEN_ARROW);
                default_case = parse_expression(parser);
                break;
            }
        }

        consume(parser, TOKEN_RBRACE);

        ASTNode* node = ast_new(AST_MATCH);
        node->data.match.value = value;
        node->data.match.patterns = patterns;
        node->data.match.bodies = bodies;
        node->data.match.case_count = case_count;
        node->data.match.default_case = default_case;
        return node;
    }

    return parse_function_call(parser);
}

ASTNode* parse_if(Parser* parser) {
    if (current_token(parser) && current_token(parser)->type == TOKEN_IF) {
        parser->pos++;
        ASTNode* condition = parse_match(parser);
        consume(parser, TOKEN_LBRACE);
        ASTNode* then_branch = parse_expression(parser);
        consume(parser, TOKEN_RBRACE);

        ASTNode* else_branch = NULL;
        if (current_token(parser) && current_token(parser)->type == TOKEN_ELSE) {
            parser->pos++;
            consume(parser, TOKEN_LBRACE);
            else_branch = parse_expression(parser);
            consume(parser, TOKEN_RBRACE);
        }

        ASTNode* node = ast_new(AST_IF);
        node->data.if_node.condition = condition;
        node->data.if_node.then_branch = then_branch;
        node->data.if_node.else_branch = else_branch;
        return node;
    }

    return parse_match(parser);
}

ASTNode* parse_expression(Parser* parser) {
    return parse_if(parser);
}

ASTNode* parse_function_def(Parser* parser) {
    consume(parser, TOKEN_FUNCTION);
    Token* name_token = consume(parser, TOKEN_IDENTIFIER);
    consume(parser, TOKEN_LPAREN);

    char** params = malloc(sizeof(char*) * 100);
    int param_count = 0;

    while (current_token(parser) && current_token(parser)->type != TOKEN_RPAREN) {
        Token* param = consume(parser, TOKEN_IDENTIFIER);
        params[param_count++] = strdup(param->value);
        if (current_token(parser) && current_token(parser)->type == TOKEN_COMMA) {
            parser->pos++;
        }
    }

    consume(parser, TOKEN_RPAREN);
    consume(parser, TOKEN_LBRACE);
    ASTNode* body = parse_expression(parser);
    consume(parser, TOKEN_RBRACE);

    ASTNode* node = ast_new(AST_FUNCTION_DEF);
    node->data.func_def.name = strdup(name_token->value);
    node->data.func_def.params = params;
    node->data.func_def.param_count = param_count;
    node->data.func_def.body = body;
    return node;
}

ASTNode* parse_statement(Parser* parser) {
    Token* token = current_token(parser);
    if (token && token->type == TOKEN_FUNCTION) {
        return parse_function_def(parser);
    }
    return parse_expression(parser);
}

Value* evaluate(ASTNode* node, Environment* env);

bool values_equal(Value* a, Value* b) {
    if (a->type != b->type) return false;

    switch (a->type) {
        case VAL_NONE:
        case VAL_NIL:
        case VAL_UNDEFINED:
        case VAL_NULL:
            return true;
        case VAL_PAIR:
            return values_equal(a->data.pair.car, b->data.pair.car) &&
                   values_equal(a->data.pair.cdr, b->data.pair.cdr);
        default:
            return false;
    }
}

int encoding_to_number(Value* encoded) {
    int count = 0;
    Value* current = encoded;

    while (current != NULL) {
        if (current->type == VAL_NIL) {
            return count;
        }
        if (current->type == VAL_PAIR && current->data.pair.car->type == VAL_NONE) {
            count++;
            current = current->data.pair.cdr;
        } else {
            return -1;
        }
    }

    return -1;
}


Value* builtin_eq(Value** args, int argc, Environment* env) {
    if (argc != 2) {
        printf("error: eq requires 2 arguments\n");
        exit(1);
    }

    return values_equal(args[0], args[1]) ? make_nil() : make_undefined();
}

Value* builtin_car(Value** args, int argc, Environment* env) {
    if (argc != 1) {
        printf("error: car requires 1 arguments\n");
        exit(1);
    }

    if (args[0]->type != VAL_PAIR) {
        printf("error: car needs a pair\n");
        exit(1);
    }

    value_retain(args[0]->data.pair.car);
    return args[0]->data.pair.car;
}

Value* builtin_cdr(Value** args, int argc, Environment* env) {
    if (argc != 1) {
        printf("error: cdr requires 1 argument\n");
        exit(1);
    }

    if (args[0]->type != VAL_PAIR) {
        printf("error: cdr needs a pair\n");
        exit(1);
    }

    value_retain(args[0]->data.pair.cdr);
    return args[0]->data.pair.cdr;
}

Value* builtin_print(Value** args, int argc, Environment* env) {
    if (argc != 1) {
        printf("error: print requires 1 pair argument\n");
        exit(1);
    }

    Value* arg = args[0];
    if (arg->type != VAL_PAIR) {
        printf("error: print needs a pair\n");
        exit(1);
    }

    Value* format_type = arg->data.pair.car;
    Value* value = arg->data.pair.cdr;

    if (format_type->type == VAL_NONE) {
        int num = encoding_to_number(value);
        if (num >= 0) {
            printf("%d", num);
        }
    }
    else if (format_type->type == VAL_UNDEFINED) {
        int ascii = encoding_to_number(value);
        if (ascii >= 0 && ascii <= 127) {
            printf("%c", (char)ascii);
        }
    }
    else if (format_type->type == VAL_NULL) {
        switch (value->type) {
            case VAL_NONE: printf("none"); break;
            case VAL_NIL: printf("nil"); break;
            case VAL_UNDEFINED: printf("undefined"); break;
            case VAL_NULL: printf("null"); break;
            case VAL_PAIR: printf("pair(...)"); break;
            default: printf("unknown"); break;
        }
    }

    fflush(stdout);
    return make_nil();
}

Value* make_builtin(const char* name, Value* (*func)(Value**, int, Environment*)) {
    Value* val = value_new(VAL_BUILTIN);
    val->data.builtin.name = strdup(name);
    val->data.builtin.func = func;
    return val;
}

void setup_minimal_builtins(Environment* env) {
    env_define(env, "eq", make_builtin("eq", builtin_eq));
    env_define(env, "car", make_builtin("car", builtin_car));
    env_define(env, "cdr", make_builtin("cdr", builtin_cdr));
    env_define(env, "print", make_builtin("print", builtin_print));
}

bool match_pattern(ASTNode* pattern, Value* value, Environment* env) {
    if (pattern->type == AST_VALUE) {
        return values_equal(pattern->data.value, value);
    } else if (pattern->type == AST_IDENTIFIER) {
        if (strcmp(pattern->data.identifier, "_") == 0) {
            return true;
        }
        env_define(env, pattern->data.identifier, value);
        return true;
    } else if (pattern->type == AST_PAIR) {
        if (value->type == VAL_PAIR) {
            return match_pattern(pattern->data.pair.car, value->data.pair.car, env) &&
                   match_pattern(pattern->data.pair.cdr, value->data.pair.cdr, env);
        }
        return false;
    } else {
        Value* pattern_value = evaluate(pattern, env);
        bool result = values_equal(pattern_value, value);
        value_release(pattern_value);
        return result;
    }
}

Value* evaluate(ASTNode* node, Environment* env) {
    switch (node->type) {
        case AST_VALUE:
            value_retain(node->data.value);
            return node->data.value;

        case AST_IDENTIFIER: {
            Value* val = env_lookup(env, node->data.identifier);
            if (!val) {
                printf("error: undefined variable %s\n", node->data.identifier);
                exit(1);
            }
            value_retain(val);
            return val;
        }

        case AST_PAIR: {
            Value* car = evaluate(node->data.pair.car, env);
            Value* cdr = evaluate(node->data.pair.cdr, env);
            Value* pair = make_pair(car, cdr);
            value_release(car);
            value_release(cdr);
            return pair;
        }

        case AST_LIST: {
            Value* result = make_nil();
            for (int i = node->data.list.count - 1; i >= 0; i--) {
                Value* element = evaluate(node->data.list.elements[i], env);
                Value* new_result = make_pair(element, result);
                value_release(element);
                value_release(result);
                result = new_result;
            }
            return result;
        }

        case AST_FUNCTION_DEF: {
            Value* func = value_new(VAL_FUNCTION);
            func->data.function.name = strdup(node->data.func_def.name);
            func->data.function.params = malloc(sizeof(char*) * node->data.func_def.param_count);
            for (int i = 0; i < node->data.func_def.param_count; i++) {
                func->data.function.params[i] = strdup(node->data.func_def.params[i]);
            }
            func->data.function.param_count = node->data.func_def.param_count;
            func->data.function.body = node->data.func_def.body;
            func->data.function.closure = env;
            env_retain(env);

            env_define(env, node->data.func_def.name, func);
            return func;
        }

        case AST_FUNCTION_CALL: {
            Value* func = evaluate(node->data.call.func, env);

            Value** args = malloc(sizeof(Value*) * node->data.call.argc);
            for (int i = 0; i < node->data.call.argc; i++) {
                args[i] = evaluate(node->data.call.args[i], env);
            }

            Value* result = NULL;

            if (func->type == VAL_BUILTIN) {
                result = func->data.builtin.func(args, node->data.call.argc, env);
            } else if (func->type == VAL_FUNCTION) {
                if (node->data.call.argc != func->data.function.param_count) {
                    printf("error: argument count mismatch\n");
                    exit(1);
                }

                Environment* call_env = env_new(func->data.function.closure);
                for (int i = 0; i < node->data.call.argc; i++) {
                    env_define(call_env, func->data.function.params[i], args[i]);
                }

                result = evaluate(func->data.function.body, call_env);
                env_release(call_env);
            } else {
                printf("error: uncallable object\n");
                exit(1);
            }

            for (int i = 0; i < node->data.call.argc; i++) {
                value_release(args[i]);
            }
            free(args);
            value_release(func);

            return result;
        }

        case AST_IF: {
            Value* condition = evaluate(node->data.if_node.condition, env);
            bool is_true = (condition->type == VAL_NIL);
            value_release(condition);

            if (is_true) {
                return evaluate(node->data.if_node.then_branch, env);
            } else if (node->data.if_node.else_branch) {
                return evaluate(node->data.if_node.else_branch, env);
            } else {
                return make_nil();
            }
        }

        case AST_MATCH: {
            Value* value = evaluate(node->data.match.value, env);

            for (int i = 0; i < node->data.match.case_count; i++) {
                Environment* match_env = env_new(env);
                if (match_pattern(node->data.match.patterns[i], value, match_env)) {
                    Value* result = evaluate(node->data.match.bodies[i], match_env);
                    env_release(match_env);
                    value_release(value);
                    return result;
                }
                env_release(match_env);
            }

            if (node->data.match.default_case) {
                Value* result = evaluate(node->data.match.default_case, env);
                value_release(value);
                return result;
            }

            printf("error: pattern matching failure\n");
            exit(1);
        }

        default:
            printf("error: unimplemented AST node\n");
            exit(1);
    }
}


void run_program(const char* program) {
    Lexer* lexer = lexer_new(program);
    Token tokens[1000];
    int token_count = 0;

    Token token;
    do {
        token = next_token(lexer);
        tokens[token_count++] = token;
    } while (token.type != TOKEN_EOF && token_count < 1000);

    lexer_free(lexer);
    Parser* parser = parser_new(tokens, token_count);

    Environment* env = env_new(NULL);
    setup_minimal_builtins(env);

    Value* last_result = NULL;
    while (current_token(parser) && current_token(parser)->type != TOKEN_EOF) {
        ASTNode* ast = parse_statement(parser);
        if (last_result) value_release(last_result);
        last_result = evaluate(ast, env);
    }

    parser_free(parser);
    env_release(env);
    if (last_result) value_release(last_result);
    for (int i = 0; i < token_count; i++) {
        free(tokens[i].value);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        FILE* file = fopen(argv[1], "r");
        if (!file) {
            printf("file not found: %s\n", argv[1]);
            return 1;
        }

        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        fseek(file, 0, SEEK_SET);

        char* program = malloc(length + 1);
        fread(program, 1, length, file);
        program[length] = '\0';
        fclose(file);

        run_program(program);
        free(program);
    } else {
        // REPL
        printf("NullScript REPL\n");

        char input[1000];
        while (1) {
            printf("nullscript> ");
          if (!fgets(input, sizeof(input), stdin)) break;

            int len = strlen(input);
            if (len > 0 && input[len-1] == '\n') {
                input[len-1] = '\0';
            }

            if (strlen(input) == 0) continue;
            if (strcmp(input, "exit") == 0) break;

            run_program(input);
        }
    }

    return 0;
}

