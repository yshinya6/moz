#include "symtable.h"
#include "karray.h"
#include "token.h"
#define KHASH_USE_FNV1A 1
#include "khash.h"

#ifdef __cplusplus
extern "C" {
#endif

DEF_ARRAY_OP(entry_t);

symtable_t *symtable_init()
{
    symtable_t *tbl = (symtable_t *)VM_MALLOC(sizeof(*tbl));
    ARRAY_init(entry_t, &tbl->table, 4);
    tbl->state = 0;
    return tbl;
}

void symtable_dispose(symtable_t *tbl)
{
    ARRAY_dispose(entry_t, &tbl->table);
    VM_FREE(tbl);
}

static void symtable_push(symtable_t *tbl, const char *tag, unsigned hash, token_t *t)
{
    entry_t entry = {};
    entry.state = tbl->state++;
    entry.hash = hash;
    entry.tag = tag;
    if (t) {
        token_copy(&entry.sym, t);
    }
    ARRAY_add(entry_t, &tbl->table, &entry);
}

void symtable_add_symbol_mask(symtable_t *tbl, const char *tableName)
{
    symtable_push(tbl, tableName, 0, NULL);
}

void symtable_add_symbol(symtable_t *tbl, const char *tableName, token_t *captured)
{
    unsigned hash = fnv1a(captured->s, captured->len);
    symtable_push(tbl, tableName, hash, captured);
}

int symtable_has_symbol(symtable_t *tbl, const char *tableName)
{
    entry_t *cur, *head;
    if (ARRAY_size(tbl->table) == 0) {
        return 0;
    }
    cur = ARRAY_last(tbl->table);
    head = ARRAY_get(entry_t, &tbl->table, 0);
    for (; cur >= head; --cur) {
        // XXX(imasahiro)
        // We assume that we do not need to check contants of `tableName`.
        if (cur->tag == tableName) {
            return cur->sym.s != NULL;
        }
    }
    return 0;
}

int symtable_get_symbol(symtable_t *tbl, const char *tableName, token_t *t)
{
    entry_t *cur, *head;
    if (ARRAY_size(tbl->table) == 0) {
        return 0;
    }

    cur = ARRAY_last(tbl->table);
    head = ARRAY_get(entry_t, &tbl->table, 0);
    for (; cur >= head; --cur) {
        if (cur->tag == tableName) {
            if (cur->sym.s != NULL) {
                token_copy(t, &cur->sym);
                return 1;
            }
        }
    }
    return 0;
}

int symtable_contains(symtable_t *tbl, const char *tableName, token_t *t)
{
    entry_t *cur, *head;
    unsigned hash;

    if (ARRAY_size(tbl->table) == 0) {
        return 0;
    }
    cur = ARRAY_last(tbl->table);
    head = ARRAY_get(entry_t, &tbl->table, 0);
    hash = fnv1a(t->s, t->len);
    for (; cur >= head; --cur) {
        if (cur->tag == tableName) {
            if (cur->sym.s == NULL) {
                return 0;
            }
            if (hash == cur->hash && token_equal(t, &cur->sym)) {
                return 1;
            }
        }
    }
    return 0;
}

void symtable_rollback(symtable_t *tbl, long saved)
{
    ARRAY_size(tbl->table) = saved;
    // if (saved == 0) {
    //     tbl->state = 0;
    // }
    // else {
    //     entry_t *last = ARRAY_last(tbl->table);
    //     tbl->state = last->state;
    // }
}

#ifdef __cplusplus
}
#endif
