/*
 * Built-in functions providing core functionality. Uses an X macro to generate
 * a computed goto to dispatch the operation.
 */

#include <math.h>
#include "lilith_int.h"
#include "builtin_symbols.h"

/*
 * A macro defining the available operations. The first argument is the goto label; the second the name
 * of the function when both params are longs; the third the function for doubles.
 */
#define IOPS $(SUB, sub_l, sub_d, "-") $(MUL, mul_l, mul_d, "*") $(DIV, div_l, div_d, "/")      \
    $(ADD, add_l, add_d, "+") $(POW, pow_l, pow_d, "^") $(MAX, max_l, max_d, "max")             \
    $(MIN, min_l, min_d, "min") $(MOD, mod_l, mod_d, "%") $(GT, gt, gt, ">") $(LT, lt, lt, "<") \
    $(GTE, gte, gte, ">=") $(LTE, lte, lte, "<=")

// Arithmetic operations
static lval *add_l(long x, long y) { return lval_long(x + y); }
static lval *add_d(double x, double y) { return lval_double(x + y); }
static lval *sub_l(long x, long y) { return lval_long(x - y); }
static lval *sub_d(double x, double y) { return lval_double(x - y); }
static lval *mul_l(long x, long y) { return lval_long(x * y); }
static lval *mul_d(double x, double y) { return lval_double(x * y); }
static lval *div_l(long x, long y) { return y == 0 ? lval_error("divide by zero") : lval_double(x / (double)y); }
static lval *div_d(double x, double y) { return y == 0.0 ? lval_error("divide by zero") : lval_double(x / y); }
static lval *max_l(long x, long y) { return lval_long(x > y ? x : y); }
static lval *max_d(double x, double y) { return lval_double(x > y ? x : y); }
static lval *min_l(long x, long y) { return lval_long(x < y ? x : y); }
static lval *min_d(double x, double y) { return lval_double(x < y ? x : y); }
static lval *mod_l(long x, long y) { return lval_long(x % y); }
static lval *mod_d(double x, double y) { return lval_double(fmod(x, y)); }
static lval *pow_l(long x, long y) { return lval_long(powl(x, y)); }
static lval *pow_d(double x, double y) { return lval_double(pow(x, y)); }
static lval *gt(double x, double y) { return lval_bool(x > y); }
static lval *lt(double x, double y) { return lval_bool(x < y); }
static lval *gte(double x, double y) { return lval_bool(x >= y); }
static lval *lte(double x, double y) { return lval_bool(x <= y); }

/**
 * The available operations. Generated by the X macro.
 */
enum iops_enum
{
#define $(X, LOP, DOP, SYM) IOPSENUM_##X,
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
#define $(X, LOP, DOP, SYM) &&JT_##X,
        IOPS
#undef $
    };

    goto *(jump_table[iop]);

#define $(X, LOP, DOP, SYM) JT_##X:                         \
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
    return rv;
    IOPS
#undef $
}

static lval *builtin_op(lenv *env, lval *a, const char* symbol, enum iops_enum iop)
{
    LASSERT_ENV(a, env, symbol);

    // Confirm that all arguments are numeric values
    for (pair *ptr = a->value.list.head; ptr; ptr = ptr->next)
    {
        LASSERT(a, ptr->data->type == LVAL_LONG || ptr->data->type == LVAL_DOUBLE,
            "function '%s' type mismatch - expected numeric, received %s",
            symbol, ltype_name(ptr->data->type));
    }

    // Get the first value
    lval *x = lval_pop(a);

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
        lval *y = lval_pop(a);
        x = do_calc(iop, x, y);
    }

    lval_del(a);
    return x;
}

// Create functions to call builtin_op() for the supported arithmetic operators
#define $(X, LOP, DOP, SYM) static lval *builtin_##X(lenv *env, lval *val) { return builtin_op(env, val, SYM, IOPSENUM_##X); }
    IOPS
#undef $

void lenv_add_builtins_sums(lenv *e)
{
#define $(X, LOP, DOP, SYM) lenv_add_builtin(e, SYM, builtin_##X);
    IOPS
#undef $
}
