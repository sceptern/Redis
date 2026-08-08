#pragma once
#include <vector>
#include "common.h"
#include "hashtable.h"
#include "heap.h"

namespace Persist {
    constexpr uint32_t MAGIC   = 0x52444231;
    constexpr uint32_t VERSION = 2;  
}

enum class SectionType : uint32_t {
    STRINGS = 1,
    ZSETS   = 2,
    VECTORS = 3,
    TTLS    = 4,
};

struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t n_sections;
};

struct SectionHeader {
    uint32_t type;
    uint32_t n_records;
};

// On-disk layout per string entry:
//   key_len (u32) | val_len (u32) | expire_at_ms (u64) | key | val
struct StringRecord {
    uint32_t key_len;
    uint32_t val_len;
    uint64_t expire_at_ms; 
};

// On-disk layout per zset entry:
//   key_len (u32) | n_members (u32) | expire_at_ms (u64) | key | members...
// Each member:
//   name_len (u32) | score (f64) | name
struct ZSetKeyRecord {
    uint32_t key_len;
    uint32_t n_members;
    uint64_t expire_at_ms;
};


bool persist_save(HMap* db, std::vector<HeapItem>& heap);
bool persist_load(HMap* db, std::vector<HeapItem>& heap);