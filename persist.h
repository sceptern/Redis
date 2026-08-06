#pragma once

#include <vector>
#include "common.h"
#include "hashtable.h"

namespace Persist {
    constexpr uint32_t MAGIC = 0x52444231;
    constexpr uint32_t VERSION = 1;
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

struct StringRecord {
    uint32_t key_len;
    uint32_t val_len;
};

bool persist_save(HMap* db);
bool persist_load(HMap* db);
bool persist_save_hmap(HMap* db);
bool persist_load_hmap(HMap* db);

