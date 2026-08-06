#pragma once

#include <string>
#include "hashtable.h"
#include "zset.h"

struct Entry {
    struct HNode node;      // hashtable node
    std::string key;
    // for TTL
    size_t heap_idx = -1;   // array index to the heap item
    // value
    uint32_t type = 0;
    // one of the following
    std::string str;
    ZSet zset;
};

enum {
    T_INIT  = 0,
    T_STR   = 1,    // string
    T_ZSET  = 2,    // sorted set
};