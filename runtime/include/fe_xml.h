/* fe_xml.h -- the front end's screen files as a small tree (fe_xml.c). */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FE_MAX_ATTR 48
typedef struct fe_node {
    char name[32];
    char *text;
    int nattr;
    char key[FE_MAX_ATTR][32];
    char *val[FE_MAX_ATTR];
    struct fe_node *child, *next;
} fe_node;

fe_node *fe_parse_bin(const uint8_t *d, uint32_t n);   /* the game's binary XML; NULL if not one */
fe_node *fe_new(const char *name);
void fe_set(fe_node *n, const char *key, const char *value);
const char *fe_get(const fe_node *n, const char *key);
void fe_del(fe_node *n, const char *key);   /* key ending in '*': every key with that prefix */
fe_node *fe_add(fe_node *parent, fe_node *after, fe_node *n);   /* after NULL: as the last child */
fe_node *fe_find(fe_node *root, const char *name, const char *id); /* id NULL: any */
fe_node *fe_clone(const fe_node *n);
void fe_free(fe_node *n);
char *fe_to_xml(const fe_node *root, size_t *len);   /* malloc'd text */
