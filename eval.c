/**
 * Eval - Simple numerical expression evaluator
 * 
 * https://github.com/mattbucknall/eval
 * 
 * Copyright (c) 2016 Matthew T. Bucknall
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISIN
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

#include "eval.h"


typedef enum
{
    EVAL_TOKEN_TYPE_END,
    EVAL_TOKEN_TYPE_ADD,
    EVAL_TOKEN_TYPE_SUBTRACT,
    EVAL_TOKEN_TYPE_MULTIPLY,
    EVAL_TOKEN_TYPE_DIVIDE,
    EVAL_TOKEN_TYPE_POWER,
    EVAL_TOKEN_TYPE_MODULE,
    EVAL_TOKEN_TYPE_OPEN_BRACKET,
    EVAL_TOKEN_TYPE_CLOSE_BRACKET,
    EVAL_TOKEN_TYPE_NUMBER,
    EVAL_TOKEN_TYPE_FUNC,
    EVAL_TOKEN_TYPE_VARIABLE
    
} EvalTokenType;


typedef struct
{
    EvalTokenType type;
    
    union
    {
         long double number;
        char name[EVAL_MAX_NAME_LENGTH];
        
    } value;
    
} EvalToken;


typedef struct
{
    const char* name;
    EvalFunc func;
    
} EvalFunctionEntry;


typedef struct
{
    const char* name;
     long double value;
    
} EvalVariableEntry;


typedef struct
{
    const EvalHooks* hooks;
    void* user_data;
    const char* input;
    size_t stack_level;
    EvalToken token;
    
} EvalContext;


static EvalResult parse_expr(EvalContext* ctx,  long double* output);


static int is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}


static int is_name_start(char c)
{
    return  ((c >= 'A') && (c <= 'Z')) ||
            ((c >= 'a') && (c <= 'z')) ||
            (c == '_');
}


static int is_name(char c)
{
    return is_name_start(c) || is_digit(c);
}


static int is_exp(char c)
{
    return (c == 'e') || (c == 'E');
}


static int is_dp(char c)
{
    return (c == '.');
}


static char get_char(EvalContext* ctx)
{
    return *(ctx->input++);
}


static void put_char(EvalContext* ctx)
{
    ctx->input--;
}


static EvalResult get_number(EvalContext* ctx)
{
    char c;
    long double value;
    long exp;
    long double power;
    
    value = 0.0dl;
    exp = 0;
    
    c = get_char(ctx);
    
    if ( !is_dp(c) )
    {
        if ( !is_digit(c) ) return EVAL_RESULT_INVALID_LITERAL;
        
        do
        {
            value = (value * (long double)(10.0)) + (long double)(c - '0');
            c = get_char(ctx);
            
        } while ( is_digit(c) );
    }
    
    if ( is_dp(c) )
    {
        c = get_char(ctx);
        if ( !is_digit(c) ) return EVAL_RESULT_INVALID_LITERAL;
        
        do
        {
            value = (value * (long double)10.0) + (long double)(c - '0');
            exp--;
            
            c = get_char(ctx);
            
        } while ( is_digit(c) );
    }
    
    if ( is_exp(c) )
    {
        int exp_neg;
        int int_val;
        
        exp_neg = 0;
        
        c = get_char(ctx);
        
        switch (c)
        {
        case '-':
            exp_neg = 1;
            /* fall through */
            
        case '+':
            c = get_char(ctx);
            /* fall through */
            
        default:
            break;
        }
        
        int_val = 0;
        
        if ( !is_digit(c) ) return EVAL_RESULT_INVALID_LITERAL;
        
        do
        {
            int_val = (int_val * 10) + (c - '0');
            c = get_char(ctx);
            
        } while ( is_digit(c) );
        
        if ( exp_neg ) exp -= int_val;
        else exp += int_val;
    }
    
    power = 10.0dl;
    
    if ( exp < 0 )
    {
        exp = -exp;
        
        while (exp)
        {
            if ( exp & 1 ) value /= power;
            exp >>= 1;
            power *= power;
        }
    }
    else
    {
        while (exp)
        {
            if ( exp & 1 ) value *= power;
            exp >>= 1;
            power *= power;
        }
    }
    
    put_char(ctx);
    
    ctx->token.type = EVAL_TOKEN_TYPE_NUMBER;
    ctx->token.value.number = value;
    
    return EVAL_RESULT_OK;
}


static EvalResult get_name(EvalContext* ctx, EvalTokenType type)
{
    char c;
    int length = 0;
    
    for (;;)
    {
        c = get_char(ctx);
        if ( !is_name(c) ) break;
        
        if ( length >= (EVAL_MAX_NAME_LENGTH - 1) ) return EVAL_RESULT_NAME_TOO_LONG;
        
        ctx->token.value.name[length++] = c;
    }
    
    put_char(ctx);
    
    ctx->token.type = type;
    ctx->token.value.name[length] = '\0';
    
    return EVAL_RESULT_OK;
}


static EvalResult get_token(EvalContext* ctx)
{
    char c;
    
    for (;;)
    {
        c = get_char(ctx);
        
        if ( c == '\0' )
        {
            put_char(ctx);
            ctx->token.type = EVAL_TOKEN_TYPE_END;
            return EVAL_RESULT_OK;
        }
        else if ( c <= ' ' )
        {
            continue;
        }
        else if ( is_digit(c) || is_dp(c) )
        {
            put_char(ctx);
            return get_number(ctx);
        }
        else if ( is_name_start(c) )
        {
            put_char(ctx);
            return get_name(ctx, EVAL_TOKEN_TYPE_FUNC);
        }
        else if ( c == '$' )
        {
            return get_name(ctx, EVAL_TOKEN_TYPE_VARIABLE);
        }
        else switch (c)
        {
        case '^':   ctx->token.type = EVAL_TOKEN_TYPE_POWER;            break;
        case '+':   ctx->token.type = EVAL_TOKEN_TYPE_ADD;              break;
        case '-':   ctx->token.type = EVAL_TOKEN_TYPE_SUBTRACT;         break;
        case '*':   ctx->token.type = EVAL_TOKEN_TYPE_MULTIPLY;         break;
        case '/':   ctx->token.type = EVAL_TOKEN_TYPE_DIVIDE;           break;
        case '(':   ctx->token.type = EVAL_TOKEN_TYPE_OPEN_BRACKET;     break;
        case ')':   ctx->token.type = EVAL_TOKEN_TYPE_CLOSE_BRACKET;    break;
        case '%':   ctx->token.type = EVAL_TOKEN_TYPE_MODULE;           break;
            
        default:    return EVAL_RESULT_ILLEGAL_CHARACTER;
        }
        
        return EVAL_RESULT_OK;
    }
}


static EvalResult parse_term(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    
    if ( ctx->token.type == EVAL_TOKEN_TYPE_NUMBER )
    {
        *output = ctx->token.value.number;
    }
    else if ( ctx->token.type == EVAL_TOKEN_TYPE_OPEN_BRACKET )
    {
        result = get_token(ctx);
        if ( result != EVAL_RESULT_OK ) return result;
        
        result = parse_expr(ctx, output);
        if ( result != EVAL_RESULT_OK ) return result;
        
        if ( ctx->token.type != EVAL_TOKEN_TYPE_CLOSE_BRACKET )
        {
            return EVAL_RESULT_EXPECTED_CLOSE_BRACKET;
        }
    }
    else if ( ctx->token.type == EVAL_TOKEN_TYPE_FUNC )
    {
        EvalFunc func;
         long double arg;
        
        if ( !ctx->hooks || !ctx->hooks->get_func )
        {
            return EVAL_RESULT_UNDEFINED_FUNCTION;
        }
        
        func = ctx->hooks->get_func(ctx->token.value.name, ctx->user_data);
        if ( !func ) return EVAL_RESULT_UNDEFINED_FUNCTION;
        
        result = get_token(ctx);
        if ( result != EVAL_RESULT_OK ) return result;
        
        if ( ctx->token.type != EVAL_TOKEN_TYPE_OPEN_BRACKET )
        {
            return EVAL_RESULT_EXPECTED_OPEN_BRACKET;
        }
        
        result = get_token(ctx);
        if ( result != EVAL_RESULT_OK ) return result;
        
        arg = 0.0f;
        
        result = parse_expr(ctx, &arg);
        if ( result != EVAL_RESULT_OK ) return result;
        
        if ( ctx->token.type != EVAL_TOKEN_TYPE_CLOSE_BRACKET )
        {
            return EVAL_RESULT_EXPECTED_CLOSE_BRACKET;
        }
        
        result = func(arg, ctx->user_data, output);
        if ( result != EVAL_RESULT_OK ) return result;
    }
    else if ( ctx->token.type == EVAL_TOKEN_TYPE_VARIABLE )
    {
        if ( !ctx->hooks || !ctx->hooks->get_variable )
        {
            return EVAL_RESULT_UNDEFINED_VARIABLE;
        }
        
        result = ctx->hooks->get_variable(ctx->token.value.name, ctx->user_data, output);
        if ( result != EVAL_RESULT_OK ) return result;
    }
    else
    {
        return EVAL_RESULT_EXPECTED_TERM;
    }
    
    return get_token(ctx);
}


static EvalResult parse_unary(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    int neg;
    long double value;
    
    neg = 0;
    value = 0.0dl;
    
    for (;;)
    {
        if ( ctx->token.type == EVAL_TOKEN_TYPE_SUBTRACT )
        {
            neg = !neg;
            
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
        }
        else break;
    }
    
    result = parse_term(ctx, &value);
    if ( result != EVAL_RESULT_OK ) return result;
    
    if ( neg ) value = -value;
    
    *output = value;
    
    return EVAL_RESULT_OK;
}


static EvalResult parse_exponential(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    long double lhs;
    long double rhs;
    
    lhs = 0.0;
    rhs = 0.0;
    
    result = parse_unary(ctx, &lhs);
    if ( result != EVAL_RESULT_OK ) return result;

	for (;;)
	{
        if ( ctx->token.type == EVAL_TOKEN_TYPE_POWER )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_unary(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;

            lhs = (long double)powl(lhs,rhs);
        }
            else break;
	}

    *output = lhs;
    
    return EVAL_RESULT_OK;
}

static EvalResult parse_product(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    long double lhs;
    long double rhs;
    
    lhs = 0.0;
    rhs = 0.0;
    
    result = parse_exponential(ctx, &lhs);
    if ( result != EVAL_RESULT_OK ) return result;
    
    for (;;)
    {
        if ( ctx->token.type == EVAL_TOKEN_TYPE_DIVIDE )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_exponential(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;
            
            lhs /= rhs;
        }
        else if ( ctx->token.type == EVAL_TOKEN_TYPE_MULTIPLY )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_exponential(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;
            
            lhs *= rhs;
        }
        else if ( ctx->token.type == EVAL_TOKEN_TYPE_MODULE )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_unary(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;
            
            lhs = (long double)fmodl(lhs,rhs);
        }
        else break;
    }
    
    *output = lhs;
    
    return EVAL_RESULT_OK;
}


static EvalResult parse_sum(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    long double lhs;
    long double rhs;
    
    lhs = 0.0;
    rhs = 0.0;

    result = parse_product(ctx, &lhs);
    if ( result != EVAL_RESULT_OK ) return result;
    
    for (;;)
    {
        if ( ctx->token.type == EVAL_TOKEN_TYPE_ADD )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_product(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;
            
            lhs += rhs;
        }
        else if ( ctx->token.type == EVAL_TOKEN_TYPE_SUBTRACT )
        {
            result = get_token(ctx);
            if ( result != EVAL_RESULT_OK ) return result;
            
            result = parse_product(ctx, &rhs);
            if ( result != EVAL_RESULT_OK ) return result;
            
            lhs -= rhs;
        }
        else break;
    }
    
    *output = lhs;
    
    return EVAL_RESULT_OK;
}


static EvalResult parse_expr(EvalContext* ctx,  long double* output)
{
    EvalResult result;
    
    if ( ctx->stack_level >= EVAL_MAX_STACK_DEPTH )
    {
        return EVAL_RESULT_STACK_OVERFLOW;
    }
    
    ctx->stack_level++;
    result = parse_sum(ctx, output);
    ctx->stack_level--;
    
    return result;
}


EvalResult eval_execute(const char* expression, const EvalHooks* hooks,
        void* user_data,  long double* output)
{
    EvalContext ctx;
    EvalResult result;
    
    ctx.hooks = hooks;
    ctx.user_data = user_data;
    ctx.input = expression;
    ctx.stack_level = 0;
    
    result = get_token(&ctx);
    if ( result != EVAL_RESULT_OK ) return result;
    
    result = parse_expr(&ctx, output);
    if ( result != EVAL_RESULT_OK ) return result;

    return ( ctx.token.type == EVAL_TOKEN_TYPE_END ) ? EVAL_RESULT_OK :
            EVAL_RESULT_UNEXPECTED_CHAR;
}


static EvalResult func_cos( long double input, void* user_data,  long double* output)
{
    *output = cosl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_sin( long double input, void* user_data,  long double* output)
{
    *output = sinl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_tan( long double input, void* user_data,  long double* output)
{
    *output = tanl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_acos( long double input, void* user_data,  long double* output)
{
    *output = acosl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_asin( long double input, void* user_data,  long double* output)
{
    *output = asinl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_atan( long double input, void* user_data,  long double* output)
{
    *output = atanl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_exp( long double input, void* user_data,  long double* output)
{
    *output = expl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_log( long double input, void* user_data,  long double* output)
{
    *output = logl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_log10( long double input, void* user_data,  long double* output)
{
    *output = log10l(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_log2( long double input, void* user_data,  long double* output)
{
    *output = log2l(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_sqrt( long double input, void* user_data,  long double* output)
{
    *output = sqrtl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_cbrt( long double input, void* user_data,  long double* output)
{
    *output = cbrtl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_ceil( long double input, void* user_data,  long double* output)
{
    *output = ceill(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_floor( long double input, void* user_data,  long double* output)
{
    *output = floorl(input);
    return EVAL_RESULT_OK;
}


static EvalResult func_round( long double input, void* user_data,  long double* output)
{
    *output = roundl(input);
    return EVAL_RESULT_OK;
}


static EvalFunc default_get_func(const char* name, void* user_data)
{
    static const EvalFunctionEntry FUNCTIONS[] =
    {
        {"cos", func_cos},
        {"sin", func_sin},
        {"tan", func_tan},
        {"acos", func_acos},
        {"asin", func_asin},
        {"atan", func_atan},
        {"exp", func_exp},
        {"log", func_log},
        {"log10", func_log10},
        {"log2", func_log2},
        {"sqrt", func_sqrt},
        {"cbrt", func_cbrt},
        {"ceil", func_ceil},
        {"floor", func_floor},
        {"round", func_round}
    };
    
    const EvalFunctionEntry* i = FUNCTIONS;
    const EvalFunctionEntry* e = i + (sizeof(FUNCTIONS) / sizeof(*FUNCTIONS));
    
    while (i != e)
    {
        if ( strcmp(i->name, name) == 0 ) return i->func;
        i++;
    }
    
    return 0;
}


static EvalResult default_get_variable(const char* name, void* user_data,  long double* output)
{
    static const EvalVariableEntry VARIABLES[] =
    {
        {"INFINITY",    INFINITY},
        {"NAN",         NAN},
        {"PI",          3.14159265358979f}
    };
    
    const EvalVariableEntry* i = VARIABLES;
    const EvalVariableEntry* e = i + (sizeof(VARIABLES) / sizeof(*VARIABLES));
    
    while (i != e)
    {
        if ( strcmp(i->name, name) == 0 )
        {
            *output = i->value;
            return EVAL_RESULT_OK;
        }
        
        i++;
    }
    
    return EVAL_RESULT_UNDEFINED_VARIABLE;
}


const EvalHooks* eval_default_hooks(void)
{
    static const EvalHooks HOOKS =
    {
        .get_func = default_get_func,
        .get_variable = default_get_variable
    };
    
    return &HOOKS;
}


const char* eval_result_to_string(EvalResult result)
{
    const char* STRS[N_EVAL_RESULT_CODES] =
    {
        "ok",
        "caracter ilegal",
        "literal invalido",
        "literal fuera de rango",
        "nombre demasiado largo",
        "caracter inesperado",
        "termino esperado",
        "desbordamiento de pila",
        "funcion indefinida",
        "variable indefinida",
        "apertura de parentesis esperado",
        "cierre de parentesis esperado"
    };
    
    return ((result >= 0) && (result < N_EVAL_RESULT_CODES)) ?
            STRS[result] : "error indefinido";
}
