/*
 * Functions for reading, constructing and printing Lisp Values.
 */

#include <stdarg.h>
#include "lilith_int.h"

bool is_escapable(char x);
char *char_escape(char x);

static lval *lval_init(unsigned type)
{
    lval *v = malloc(sizeof(lval));
    v->type = type;
    return v;
}

static void lval_expr_print(const lval *v, char open, char close, unsigned options)
{
    putchar(open);
    for (pair *ptr = v->value.list.head; ptr; ptr = ptr->next)
    {
        if (ptr != v->value.list.head)
        {
            putchar(' ');
        }

        lval_print(ptr->data, options);
    }

    putchar(close);
}

static void lval_print_string(const lval *str_val)
{
    putchar('"');
    size_t len = strlen(str_val->value.str_val);
    for (size_t i = 0; i < len; i++)
    {
        if (is_escapable(str_val->value.str_val[i]))
        {
            printf("%s", char_escape(str_val->value.str_val[i]));
        }
        else
        {
            putchar(str_val->value.str_val[i]);
        }
    }

    putchar('"');
}

lval *lval_expr_item(lval *val, unsigned i)
{
    unsigned expr_item = 0;
    for (pair *ptr = val->value.list.head; ptr; ptr = ptr->next)
    {
        if (expr_item++ == i)
        {
            return ptr->data;
        }
    }

    return 0;
}

lval *lval_pop(lval *val)
{
    pair *rv = val->value.list.head;
    val->value.list.head = rv->next;
    rv->next = 0;
    LVAL_EXPR_CNT(val)--;

    lval *r = rv->data;
    free(rv);
    return r;
}

lval *lval_take(lval *val, unsigned i)
{
    unsigned cnt = 0;
    while (cnt++ < i)
    {
        lval_del(lval_pop(val));
    }

    lval *rv = lval_pop(val);

    while (LVAL_EXPR_CNT(val))
    {
        lval_del(lval_pop(val));
    }

    lval_del(val);
    return rv;
}

lval *lval_error(const char *fmt, ...)
{
    lval *v = lval_init(LVAL_ERROR);

    // Create a va list and initialize it
    va_list va;
    va_start(va, fmt);

    // Allocate 512 bytes of space
    v->value.str_val = malloc(512);

    // printf the error string with a maximum of 511 characters
    vsnprintf(v->value.str_val, 511, fmt, va);

    // Reallocate to number of bytes actually used
    v->value.str_val = realloc(v->value.str_val, strlen(v->value.str_val) + 1);

    // Cleanup our va list
    va_end(va);
    return v;
}

lval *lval_long(long num)
{
    lval *rv = lval_init(LVAL_LONG);
    rv->value.num_l = num;
    return rv;
}

lval *lval_double(double num)
{
    lval *rv = lval_init(LVAL_DOUBLE);
    rv->value.num_d = num;
    return rv;
}

lval *lval_bool(bool bval)
{
    lval *rv = lval_init(LVAL_BOOL);
    rv->value.bval = bval;
    return rv;
}

lval *lval_string(const char *string)
{
    lval *rv = lval_init(LVAL_STRING);
    rv->value.str_val = malloc(strlen(string) + 1);
    strcpy(rv->value.str_val, string);
    return rv;
}

lval *lval_symbol(const char *symbol)
{
    lval *rv = lval_init(LVAL_SYMBOL);
    rv->value.str_val = malloc(strlen(symbol) + 1);
    strcpy(rv->value.str_val, symbol);
    return rv;
}

lval *lval_sexpression()
{
    lval *rv = lval_init(LVAL_SEXPRESSION);
    rv->value.list.count = 0;
    rv->value.list.head = 0;
    return rv;
}

lval *lval_qexpression()
{
    lval *rv = lval_init(LVAL_QEXPRESSION);
    rv->value.list.count = 0;
    rv->value.list.head = 0;
    return rv;
}

lval *lval_fun(lbuiltin function)
{
    lval *rv = lval_init(LVAL_BUILTIN_FUN);
    rv->value.builtin = function;
    return rv;
}

lval *lval_lambda(lval *formals, lval* body)
{
    lval *rv = lval_init(LVAL_USER_FUN);

    // Build new environment
    rv->value.user_fun.env = lenv_new();

    // Set formals and body
    rv->value.user_fun.formals = formals;
    rv->value.user_fun.body = body;
    return rv;
}

lval *lval_add(lval *v, lval *x)
{
    v->value.list.count++;
    pair **ptr = &v->value.list.head;
    for (; *ptr; ptr = &(*ptr)->next)
    {
    }

    pair *n = malloc(sizeof(pair));
    n->next = 0;
    n->data = x;
    *ptr = n;
    return v;
}

void lval_print(const lval *v, unsigned options)
{
    switch (v->type)
    {
    case LVAL_LONG:
        printf("%li", v->value.num_l);
        break;
    case LVAL_DOUBLE:
        printf("%f", v->value.num_d);
        break;
    case LVAL_BOOL:
        printf("%s", v->value.bval ? "#t" : "#f");
        break;
    case LVAL_STRING:
        if (!options)
        {
            lval_print_string(v);
        }
        else
        {
            printf("%s", v->value.str_val);
        }
        break;
    case LVAL_SYMBOL:
        printf("%s", v->value.str_val);
        break;
    case LVAL_ERROR:
        printf("Error: %s", v->value.str_val);
        break;
    case LVAL_BUILTIN_FUN:
        printf("<builtin>");
        break;
    case LVAL_SEXPRESSION:
        lval_expr_print(v, '(', ')', options);
        break;
    case LVAL_QEXPRESSION:
        lval_expr_print(v, '{', '}', options);
        break;
    case LVAL_USER_FUN:
        printf("(\\ ");
        lval_print(v->value.user_fun.formals, options);
        putchar(' ');
        lval_print(v->value.user_fun.body, options);
        putchar(')');
        break;
    }
}

bool lval_is_equal(lval *x, lval *y)
{
    if (x->type != y->type)
    {
        if (x->type == LVAL_LONG && y->type == LVAL_DOUBLE)
        {
            return x->value.num_l == y->value.num_d;
        }
        else if (x->type == LVAL_DOUBLE && y->type == LVAL_LONG)
        {
            return x->value.num_d == y->value.num_l;
        }

        return 0;
    }

    switch (x->type)
    {
    case LVAL_LONG:
        return x->value.num_l == y->value.num_l;
    case LVAL_DOUBLE:
        return x->value.num_d == y->value.num_d;
    case LVAL_BOOL:
        return x->value.bval == y->value.bval;
    case LVAL_STRING:
    case LVAL_ERROR:
    case LVAL_SYMBOL:
        return (strcmp(x->value.str_val, y->value.str_val) == 0);
    case LVAL_BUILTIN_FUN:
        return x->value.builtin == y->value.builtin;
    case LVAL_USER_FUN:
        return lval_is_equal(x->value.user_fun.formals, y->value.user_fun.formals) &&
            lval_is_equal(x->value.user_fun.body, y->value.user_fun.body);
    case LVAL_QEXPRESSION:
    case LVAL_SEXPRESSION:
        if (LVAL_EXPR_CNT(x) != LVAL_EXPR_CNT(y))
        {
            return false;
        }

        for (pair *ptrx = x->value.list.head, *ptry = x->value.list.head;
             ptrx && ptry;
             ptrx = ptrx->next, ptry = ptry->next)
        {
            if (!lval_is_equal(ptrx->data, ptry->data))
            {
                return 0;
            }
        }

        return true;
    }

    return false; 
}

void lval_del(lval *v)
{
    pair *tmp, *ptr;

    switch (v->type)
    {
    case LVAL_LONG:
    case LVAL_DOUBLE:
    case LVAL_BUILTIN_FUN:
    case LVAL_BOOL:
        break;
    case LVAL_STRING:
    case LVAL_ERROR:
    case LVAL_SYMBOL:
        free(v->value.str_val);
        break;
    case LVAL_SEXPRESSION:
    case LVAL_QEXPRESSION:
        ptr = v->value.list.head;
        while (ptr)
        {
            tmp = ptr;
            ptr = ptr->next;
            lval_del(tmp->data);
            free(tmp);
        }
        break;
    case LVAL_USER_FUN:
        lenv_del(v->value.user_fun.env);
        lval_del(v->value.user_fun.formals);
        lval_del(v->value.user_fun.body);
        break;
    }

    free(v);
}

lval *lval_copy(lval *v)
{
    lval *rv = lval_init(v->type);

    switch (v->type)
    {
    case LVAL_LONG:
        rv->value.num_l = v->value.num_l;
        break;
    case LVAL_DOUBLE:
        rv->value.num_d = v->value.num_d;
        break;
    case LVAL_BOOL:
        rv->value.bval = v->value.bval;
        break;
    case LVAL_STRING:
    case LVAL_ERROR:
    case LVAL_SYMBOL:
        rv->value.str_val = malloc(strlen(v->value.str_val) + 1);
        strcpy(rv->value.str_val, v->value.str_val);
        break;
    case LVAL_BUILTIN_FUN:
        rv->value.builtin = v->value.builtin;
        break;
    case LVAL_QEXPRESSION:
    case LVAL_SEXPRESSION:
        rv->value.list.count = 0;
        rv->value.list.head = 0;
        for (pair *ptr = v->value.list.head; ptr; ptr = ptr->next)
        {
            lval_add(rv, lval_copy(ptr->data));
        }
        break;
    case LVAL_USER_FUN:
        rv->value.user_fun.env = lenv_copy(v->value.user_fun.env);
        rv->value.user_fun.formals = lval_copy(v->value.user_fun.formals);
        rv->value.user_fun.body = lval_copy(v->value.user_fun.body);
        break;
    }

    return rv;
}

char *ltype_name(unsigned type)
{
    switch(type)
    {
        case LVAL_BUILTIN_FUN:
        case LVAL_USER_FUN:
            return "Function";
        case LVAL_LONG:
            return "Number";
        case LVAL_DOUBLE:
            return "Decimal";
        case LVAL_BOOL:
            return "Boolean";
        case LVAL_STRING:
            return "String";
        case LVAL_ERROR:
            return "Error";
        case LVAL_SYMBOL:
            return "Symbol";
        case LVAL_SEXPRESSION:
            return "S-Expression";
        case LVAL_QEXPRESSION:
            return "Q-Expression";
        default:
            return "Unknown";
    }
}

void lilith_println(const lval *val)
{
    lval_print(val, 0);
    putchar('\n');
}

void lilith_lval_del(lval *val)
{
    lval_del(val);
}
