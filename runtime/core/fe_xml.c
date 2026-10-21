/* fe_xml.c -- the front end's screen files (.rfe) as a small tree (pt.menu; docs/PARTY_RESEARCH.md "Main
 * menu item"). The game's loader (0x5A06D0 -> Hbs::Xml::Document 0x6CF7D0) reads XML text or its own
 * binary XML; the party's screens are written as text, and the main menu (binary, the user's own data)
 * is read here, given its ONLINE item and handed back as text.
 * Binary layout: "00 BE EF FF", element records, a table of N u32 offsets, N, a string pool of L bytes,
 * L. An element is 9 u16: name, text, parent, previous, next, first attribute, last attribute, first
 * child, last child (table indices from 1, 0 = none); an attribute is {u16 name, u16 value}, the ones of
 * an element contiguous from first to last; strings are pool offsets from 1. The root is at byte 4. */
#include "fe_xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

typedef struct {
    const uint8_t *d;
    uint32_t n, ntab, tab, pool, plen;
} bin_t;

static const char *pool_str(const bin_t *b, uint16_t v) {
    if (!v || v > b->plen) return "";
    return (const char *)b->d + b->pool + v - 1;
}

static uint32_t at(const bin_t *b, uint16_t idx) {   /* table index (from 1) -> byte offset, 0 if bad */
    if (!idx || idx > b->ntab) return 0;
    uint32_t o = u32(b->d + b->tab + 4u * (idx - 1u));
    return o + 18 <= b->tab ? o : 0;
}

fe_node *fe_new(const char *name) {
    fe_node *n = calloc(1, sizeof *n);
    if (n) snprintf(n->name, sizeof n->name, "%s", name);
    return n;
}

void fe_set(fe_node *n, const char *key, const char *value) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->key[i], key)) {
            free(n->val[i]);
            n->val[i] = strdup(value);
            return;
        }
    if (n->nattr == FE_MAX_ATTR) return;
    snprintf(n->key[n->nattr], sizeof n->key[0], "%s", key);
    n->val[n->nattr++] = strdup(value);
}

void fe_del(fe_node *n, const char *key) {
    for (int i = n->nattr - 1; i >= 0; i--)
        if (!strcmp(n->key[i], key) || (key[strlen(key) - 1] == '*' && !strncmp(n->key[i], key, strlen(key) - 1))) {
            free(n->val[i]);
            n->nattr--;
            memmove(n->key + i, n->key + i + 1, sizeof n->key[0] * (size_t)(n->nattr - i));
            memmove((void *)(n->val + i), (const void *)(n->val + i + 1), sizeof n->val[0] * (size_t)(n->nattr - i));
        }
}

const char *fe_get(const fe_node *n, const char *key) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->key[i], key)) return n->val[i];
    return NULL;
}

fe_node *fe_add(fe_node *parent, fe_node *after, fe_node *n) {   /* after = NULL: last child */
    if (!n) return NULL;
    if (after) {
        n->next = after->next;
        after->next = n;
        return n;
    }
    fe_node **p = &parent->child;
    while (*p) p = &(*p)->next;
    *p = n;
    return n;
}

static fe_node *element(const bin_t *b, uint32_t o, int depth) {
    if (!o || depth > 32) return NULL;
    const uint8_t *r = b->d + o;
    fe_node *n = fe_new(pool_str(b, u16(r)));
    if (!n) return NULL;
    if (u16(r + 2)) n->text = strdup(pool_str(b, u16(r + 2)));
    uint32_t a = at(b, u16(r + 10)), last = at(b, u16(r + 12));
    for (int i = 0; a && i < FE_MAX_ATTR && a + 4 <= b->tab; i++, a += 4) {
        fe_set(n, pool_str(b, u16(b->d + a)), pool_str(b, u16(b->d + a + 2)));
        if (a == last) break;
    }
    for (uint16_t c = u16(r + 14); c; c = u16(b->d + at(b, c) + 8)) {
        if (!at(b, c)) break;
        fe_add(n, NULL, element(b, at(b, c), depth + 1));
    }
    return n;
}

fe_node *fe_parse_bin(const uint8_t *d, uint32_t n) {
    static const uint8_t magic[4] = { 0x00, 0xBE, 0xEF, 0xFF };
    if (n < 32 || memcmp(d, magic, 4) != 0) return NULL;
    bin_t b = { .d = d, .n = n, .plen = u32(d + n - 4) };
    if (b.plen + 8 > n) return NULL;
    b.pool = n - 4 - b.plen;
    b.ntab = u32(d + b.pool - 4);
    if (4ull * b.ntab + 8 > b.pool) return NULL;
    b.tab = b.pool - 4 - 4 * b.ntab;
    return element(&b, 4, 0);
}

fe_node *fe_find(fe_node *root, const char *name, const char *id) {   /* first descendant <name ID=id> */
    for (fe_node *c = root ? root->child : NULL; c; c = c->next) {
        const char *v = fe_get(c, "ID");
        if (!strcmp(c->name, name) && (!id || (v && !strcmp(v, id)))) return c;
        fe_node *f = fe_find(c, name, id);
        if (f) return f;
    }
    return NULL;
}

fe_node *fe_clone(const fe_node *n) {
    fe_node *c = fe_new(n->name);
    if (!c) return NULL;
    if (n->text) c->text = strdup(n->text);
    for (int i = 0; i < n->nattr; i++) fe_set(c, n->key[i], n->val[i]);
    for (const fe_node *k = n->child; k; k = k->next) fe_add(c, NULL, fe_clone(k));
    return c;
}

void fe_free(fe_node *n) {
    while (n) {
        fe_node *next = n->next;
        fe_free(n->child);
        for (int i = 0; i < n->nattr; i++) free(n->val[i]);
        free(n->text);
        free(n);
        n = next;
    }
}

typedef struct {
    char *s;
    size_t n, cap;
} buf_t;

static void put(buf_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = (b->n + n + 1) * 2;
        char *g = realloc(b->s, cap);
        if (!g) return;
        b->s = g;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void puts_(buf_t *b, const char *s) { put(b, s, strlen(s)); }
static void put_escaped(buf_t *b, const char *s) {
    for (; *s; s++) {
        const char *e = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : NULL;
        if (e) puts_(b, e);
        else put(b, s, 1);
    }
}

static void xml(buf_t *b, const fe_node *n) {
    puts_(b, "<");
    puts_(b, n->name);
    for (int i = 0; i < n->nattr; i++) {
        puts_(b, " ");
        puts_(b, n->key[i]);
        puts_(b, "=\"");
        put_escaped(b, n->val[i]);
        puts_(b, "\"");
    }
    if (!n->child && !n->text) {
        puts_(b, "/>\n");
        return;
    }
    puts_(b, ">");
    if (n->text) put_escaped(b, n->text);
    puts_(b, "\n");
    for (const fe_node *c = n->child; c; c = c->next) xml(b, c);
    puts_(b, "</");
    puts_(b, n->name);
    puts_(b, ">\n");
}

char *fe_to_xml(const fe_node *root, size_t *len) {
    buf_t b = { 0 };
    puts_(&b, "<?xml version=\"1.0\" ?>\n");
    xml(&b, root);
    *len = b.n;
    return b.s;
}
