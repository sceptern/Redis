#include <assert.h>
#include <string.h>
#include <stdlib.h>
// proj
#include "zset.h"
#include "common.h"

static ZNode* znode_new(const char* name, size_t len, double score) {
    ZNode* node = (ZNode *)malloc(sizeof(ZNode) + len);
    assert(node); // not a good idea in real projects
    avl_init(&node->tree_link);
    node->hmap_link.next = NULL;
    node->hmap_link.hcode = str_hash((uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

static void znode_del(ZNode* node) {
    free(node);
}

static size_t min(size_t lhs, size_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

