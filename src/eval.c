/*
 * Functions to evaluate an s-expression. Uses an X macro to generate
 * a computed goto to dispatch the operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mpc.h"
#include "lilith_int.h"

#define LASSERT(args, cond, fmt, ...)                   \
    do {                                                \
        if (!(cond))                                    \
        {                                               \
            lval *err = lval_error(fmt, ##__VA_ARGS__); \
            lval_del(args);                             \
            return err;                                 \
        }                                               \
    } while (0)

#define LASSERT_ENV(arg, arg_env, arg_symbol) \
    LASSERT(arg, arg_env != 0, "environment not set for '%s'", arg_symbol)

#define LASSERT_NUM_ARGS(arg, expected, arg_symbol)       \
    LASSERT(arg, LVAL_EXPR_CNT(arg) == expected,          \
        "function '%s' expects %d argument, received %d", \
        arg_symbol, expected, LVAL_EXPR_CNT(arg))

#define LASSERT_TYPE_ARG(arg, idx, expected, arg_symbol)          \
    LASSERT(arg, LVAL_EXPR_ITEM(arg, idx)->type == expected,      \
        "function '%s' type mismatch - expected %s, received %s", \
        arg_symbol, ltype_name(expected), ltype_name(LVAL_EXPR_ITEM(arg, idx)->type))

#define LVAL_EXPR_CNT(arg) arg->value.list.count
#define LVAL_EXPR_LST(arg) arg->value.list.cell
#define LVAL_EXPR_ITEM(arg, i) arg->value.list.cell[i]

/*
 * A macro defining the available operations. The first argument is the goto label; the second the name
 * of the function when both params are longs; the third the function for doubles.
 */
#define IOPS $(SUB, sub_l, sub_d) $(MUL, mul_l, mul_d) $(DIV, div_l, div_d) $(ADD, add_l, add_d) \
    $(POW, pow_l, pow_d) $(MAX, max_l, max_d) $(MIN, min_l, min_d) $(MOD, mod_l, mod_d)

// Arithmetic operations
static lval *add_l(long x, long y) { return lval_long(x + y); }
static lval *add_d(double x, double y) { return lval_double(x + y); }
static lval *sub_l(long x, long y) { return lval_long(x - y); }
static lval *sub_d(double x, double y) { return lval_double(x - y); }
static lval *mul_l(long x, long y) { return lval_long(x * y); }
static lval *mul_d(double x, double y) { return lval_double(x * y); }
static lval *div_l(long x, long y) { return y == 0 ? lval_error("divide by zero") : lval_long(x / y); }
static lval *div_d(double x, double y) { return y == 0.0 ? lval_error("divide by zero") : lval_double(x / y); }
static lval *max_l(long x, long y) { return lval_long(x > y ? x : y); }
static lval *max_d(double x, double y) { return lval_double(x > y ? x : y); }
static lval *min_l(long x, long y) { return lval_long(x < y ? x : y); }
static lval *min_d(double x, double y) { return lval_double(x < y ? x : y); }
static lval *mod_l(long x, long y) { return lval_long(x % y); }
static lval *mod_d(double x, double y) { return lval_double(fmod(x, y)); }
static lval *pow_l(long x, long y) { return lval_long(powl(x, y)); }
static lval *pow_d(double x, double y) { return lval_double(pow(x, y)); }

/**
 * The available operations. Generated by the X macro.
 */
enum iops_enum
{
#define $(X, LOP, DOP) IOPSENUM_##X,
    IOPS
#undef $
};

/**
 * Performs a calculation for two lvals.
 * 
 * @param iop  the operation to perform
 * @param xval the first argument -- freed in this function
 * @param yval the second argument -- freed in this function
 * @returns    a new lval with the result
 */
static lval *do_calc(enum iops_enum iop, lval *xval, lval *yval)
{
    lval *rv;
    static void *jump_table[] =
    {
#define $(X, LOP, DOP) &&JT_##X,
        IOPS
#undef $
    };

    goto *(jump_table[iop]);

#define $(X, LOP, DOP) JT_##X:                              \
    if (xval->type == LVAL_LONG && yval->type == LVAL_LONG) \
    {                                                       \
        rv = LOP(xval->value.num_l, yval->value.num_l);     \
    }                                                       \
    else if (xval->type == LVAL_LONG)                       \
    {                                                       \
        rv = DOP(xval->value.num_l, yval->value.num_d);     \
    }                                                       \
    else if (yval->type == LVAL_LONG)                       \
    {                                                       \
        rv = DOP(xval->value.num_d, yval->value.num_l);     \
    }                                                       \
    else                                                    \
    {                                                       \
        rv = DOP(xval->value.num_d, yval->value.num_d);     \
    }                                                       \
    lval_del(xval);                                         \
    lval_del(yval);                                         \
    return rv;
    IOPS
#undef $
}

static lval *lval_pop(lval *val, int i)
{
    lval *x = LVAL_EXPR_ITEM(val, i);

    // Shift memory after the item at "i" over the top
    memmove(&LVAL_EXPR_ITEM(val, i), &LVAL_EXPR_ITEM(val, i + 1),
        sizeof(lval*) * (LVAL_EXPR_CNT(val) - i - 1));

    // Decrease the count of items in the list
    LVAL_EXPR_CNT(val)--;

    // Reallocate the memory used
    LVAL_EXPR_LST(val) = realloc(LVAL_EXPR_LST(val), sizeof(lval*) * LVAL_EXPR_CNT(val));
    return x;
}

static lval *lval_take(lval *val, int i)
{
    lval *x = lval_pop(val, i);
    lval_del(val);
    return x;
}

/**
 * Built-in function to return the first element of a q-expression.
 */
static lval *builtin_head(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 1, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);
    LASSERT(val, LVAL_EXPR_CNT(LVAL_EXPR_ITEM(val, 0)) != 0, "empty q-expression passed to '%s'", symbol);

    lval *rv = lval_take(val, 0);
    while (rv->value.list.count > 1)
    {
        lval_del(lval_pop(rv, 1));
    }

    return rv;
}

/**
 * Built-in function to return all elements of a q-expression except the first.
 */
static lval *builtin_tail(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 1, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);
    LASSERT(val, LVAL_EXPR_CNT(LVAL_EXPR_ITEM(val, 0)) != 0, "empty q-expression passed to '%s'", symbol);

    lval *rv = lval_take(val, 0);
    lval_del(lval_pop(rv, 0));
    return rv;
}

/**
 * Built-in function to evaluate a q-expression.
 */
static lval *builtin_eval(lenv* env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 1, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);

    lval *x = lval_take(val, 0);
    x->type = LVAL_SEXPRESSION;
    return lval_eval(env, x);
}

/**
 * Built-in function to convert an s-expression in to a q-expression.
 */
static lval *builtin_list(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    val->type = LVAL_QEXPRESSION;
    return val;
}

/**
 * Add all elements of the second q-expression to the first.
 */
static lval *lval_join(lval *x, lval* y)
{
    while (LVAL_EXPR_CNT(y))
    {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}

/**
 * Built-in function to join q-expressions together.
 */
static lval *builtin_join(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);

    for (int i = 0; i < LVAL_EXPR_CNT(val); i++)
    {
        LASSERT_TYPE_ARG(val, i, LVAL_QEXPRESSION, symbol);
    }

    lval *x = lval_pop(val, 0);
    while (LVAL_EXPR_CNT(val))
    {
        x = lval_join(x, lval_pop(val, 0));
    }

    lval_del(val);
    return x;
}

/**
 * Built-in function to return the number of items in a q-expression.
 */
static lval *builtin_len(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 1, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);

    lval *x = lval_take(val, 0);
    return lval_long(LVAL_EXPR_CNT(x));
}

/**
 * Built-in function to add an element to the start of a q-expression.
 */
static lval *builtin_cons(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 2, symbol);
    LASSERT(val,
        LVAL_EXPR_ITEM(val, 0)->type == LVAL_LONG || LVAL_EXPR_ITEM(val, 0)->type == LVAL_DOUBLE || LVAL_EXPR_ITEM(val, 0)->type == LVAL_FUN,
        "first '%s' parameter should be a value or a function", symbol);
    LASSERT(val, LVAL_EXPR_ITEM(val, 1)->type == LVAL_QEXPRESSION, "second '%s' parameter should be a q-expression", symbol);

    lval *rv = lval_qexpression();
    rv = lval_add(rv, lval_pop(val, 0));
    while (LVAL_EXPR_CNT(LVAL_EXPR_ITEM(val, 1)))
    {
        rv = lval_add(rv, lval_pop(LVAL_EXPR_ITEM(val, 1), 0));
    }
    
    lval_del(val);
    return rv;
}

/**
 * Built-in function to return all elements in a q-expression except the first.
 */
static lval *builtin_init(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_NUM_ARGS(val, 1, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);
    LASSERT(val, LVAL_EXPR_CNT(LVAL_EXPR_ITEM(val, 0)) != 0, "empty q-expression passed to '%s'", symbol);

    lval *rv = lval_take(val, 0);
    lval_del(lval_pop(rv, LVAL_EXPR_CNT(rv) - 1));
    return rv;
}

static lval *builtin_op(lenv *env, const char *symbol, lval *a, enum iops_enum iop)
{
    LASSERT_ENV(a, env, symbol);

    // Confirm that all arguments are numeric values
    for (int i = 0; i < LVAL_EXPR_CNT(a); i++)
    {
        LASSERT(a, LVAL_EXPR_ITEM(a, i)->type == LVAL_LONG || LVAL_EXPR_ITEM(a, i)->type == LVAL_DOUBLE,
            "function '%s' type mismatch - expected numeric, received %s",
            symbol, ltype_name(LVAL_EXPR_ITEM(a, i)->type));
    }

    // Get the first value
    lval *x = lval_pop(a, 0);

    // If single arument subtraction, negate value
    if (LVAL_EXPR_CNT(a) == 0 && (iop == IOPSENUM_SUB))
    {
        if (x->type == LVAL_LONG)
        {
            x->value.num_l = -x->value.num_l;
        }
        else
        {
            x->value.num_d = -x->value.num_d;
        }
    }

    // While elements remain
    while (LVAL_EXPR_CNT(a) > 0)
    {
        lval *y = lval_pop(a, 0);
        x = do_calc(iop, x, y);
    }

    lval_del(a);
    return x;
}

/**
 * Built-in function for defining new symbols. First argument in val's list
 * is a q-expression with one or more symbols. Additional arguments are values
 * that map to those symbols.
 */
static lval *builtin_def(lenv *env, const char *symbol, lval *val)
{
    LASSERT_ENV(val, env, symbol);
    LASSERT_TYPE_ARG(val, 0, LVAL_QEXPRESSION, symbol);

    // First argument is a symbol list
    lval *syms = LVAL_EXPR_ITEM(val, 0);
    for (int i = 0; i < LVAL_EXPR_CNT(syms); i++)
    {
        LASSERT(val, LVAL_EXPR_ITEM(syms, i)->type == LVAL_SYMBOL,
            "function '%s' type mismatch - expected %s, received %s",
            symbol, ltype_name(LVAL_SYMBOL), ltype_name(LVAL_EXPR_ITEM(syms, i)->type));
    }

    LASSERT(val, LVAL_EXPR_CNT(syms) == LVAL_EXPR_CNT(val) - 1,
        "function '%s' argument mismatch - %d symbols, %d values",
        symbol, LVAL_EXPR_CNT(syms), LVAL_EXPR_CNT(val) - 1);
    
    // Assign symbols to values
    for (int i = 0; i < LVAL_EXPR_CNT(syms); i++)
    {
        LASSERT(val, !lenv_put(env, LVAL_EXPR_ITEM(syms, i), LVAL_EXPR_ITEM(val, i + 1)),
            "function '%s' is a built-in", LVAL_EXPR_ITEM(syms, i)->value.symbol);
    }

    lval_del(val);
    return lval_sexpression();
}

static lval *builtin_add(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_ADD); }
static lval *builtin_sub(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_SUB); }
static lval *builtin_div(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_DIV); }
static lval *builtin_mul(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_MUL); }
static lval *builtin_min(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_MIN); }
static lval *builtin_max(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_MAX); }
static lval *builtin_pow(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_POW); }
static lval *builtin_mod(lenv *env, const char *symbol, lval *val) { return builtin_op(env, symbol, val, IOPSENUM_MOD); }

static void lenv_add_builtin(lenv *env, char *name, lbuiltin func)
{
    lval *k = lval_symbol(name);
    lval *v = lval_fun(name, func);

    lenv_put_builtin(env, k, v);
    lval_del(k);
    lval_del(v);
}

static lval *lval_eval_sexpr(lenv *env, lval *val)
{
    // Evaluate children
    for (int i = 0; i < LVAL_EXPR_CNT(val); i++)
    {
        LVAL_EXPR_ITEM(val, i) = lval_eval(env, LVAL_EXPR_ITEM(val, i));
    }

    // Check for errors
    for (int i = 0; i < LVAL_EXPR_CNT(val); i++)
    {
        if (LVAL_EXPR_ITEM(val, i)->type == LVAL_ERROR)
        {
            return lval_take(val, i);
        }
    }

    // Empty expressions
    if (LVAL_EXPR_CNT(val) == 0)
    {
        return val;
    }

    // Single expression
    if (LVAL_EXPR_CNT(val) == 1)
    {
        return lval_take(val, 0);
    }

    // First element must be a function
    lval *first = lval_pop(val, 0);
    if (first->type != LVAL_FUN)
    {
        lval_del(first);
        lval_del(val);
        return lval_error("s-expression does not start with function, '%s'", ltype_name(first->type));
    }

    // Call function
    lval *result = first->value.fhandle.fun(env, first->value.fhandle.symbol, val);
    lval_del(first);
    return result;
}

lval *lval_eval(lenv *env, lval *val)
{
    // Lookup the function and return
    if (val->type == LVAL_SYMBOL)
    {
        lval *x = lenv_get(env, val);
        lval_del(val);
        return x;
    }

    // Evaluate Sexpressions
    if (val->type == LVAL_SEXPRESSION)
    {
        return lval_eval_sexpr(env, val);
    }

    // All other lval types remain the same
    return val;
}

void lenv_add_builtins(lenv *e)
{
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
    lenv_add_builtin(e, "min", builtin_min);
    lenv_add_builtin(e, "max", builtin_max);
    lenv_add_builtin(e, "^", builtin_pow);
    lenv_add_builtin(e, "%", builtin_mod);

    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "len", builtin_len);
    lenv_add_builtin(e, "cons", builtin_cons);
    lenv_add_builtin(e, "init", builtin_init);
}
