#include "persist.h"
#include "types.h"   
#include "zset.h"    
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>

// ──────────────────────────────────────────────────────
// Utilities
// ──────────────────────────────────────────────────────

static uint64_t persist_now_ms() {
    struct timespec tv = {};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

// Typed write helpers — each returns false on failure
static bool fw_u32(FILE* f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1; }
static bool fw_u64(FILE* f, uint64_t v) { return fwrite(&v, 8, 1, f) == 1; }
static bool fw_dbl(FILE* f, double  v) { return fwrite(&v, 8, 1, f) == 1; }
static bool fw_str(FILE* f, const char* p, uint32_t n) {
    return n == 0 || fwrite(p, n, 1, f) == 1;
}

static bool fr_u32(FILE* f, uint32_t& v) { return fread(&v, 4, 1, f) == 1; }
static bool fr_u64(FILE* f, uint64_t& v) { return fread(&v, 8, 1, f) == 1; }
static bool fr_dbl(FILE* f, double&  v) { return fread(&v, 8, 1, f) == 1; }
static bool fr_str(FILE* f, std::string& s, uint32_t n) {
    s.resize(n);
    return n == 0 || fread(&s[0], n, 1, f) == 1;
}

static void persist_heap_insert(std::vector<HeapItem>& heap, Entry* ent, uint64_t expire_at) {
    HeapItem item = {expire_at, &ent->heap_idx};
    size_t pos = heap.size();
    heap.push_back(item);
    heap_update(heap.data(), pos, heap.size());
    
}



static void count_tab(HTab* tab, uint32_t& n_str, uint32_t& n_zset) {
    if (tab->mask == 0) return;
    for (size_t i = 0; i <= tab->mask; i++) {
        for (HNode* n = tab->tab[i]; n; n = n->next) {
            Entry* ent = container_of(n, Entry, node);
            if      (ent->type == T_STR)  n_str++;
            else if (ent->type == T_ZSET) n_zset++;
        }
    }
}

static void count_entries(HMap* db, uint32_t& n_str, uint32_t& n_zset) {
    n_str = n_zset = 0;
    count_tab(&db->newer, n_str, n_zset);
    count_tab(&db->older, n_str, n_zset);
}

// ──────────────────────────────────────────────────────
// Save
// ──────────────────────────────────────────────────────

static bool save_strings(HTab* tab, FILE* f, std::vector<HeapItem>& heap) {
    if (tab->mask == 0) return true;
    for (size_t i = 0; i <= tab->mask; i++) {
        for (HNode* n = tab->tab[i]; n; n = n->next) {
            Entry* ent = container_of(n, Entry, node);
            if (ent->type != T_STR) continue;

            uint64_t exp = (ent->heap_idx != (size_t)-1) ? heap[ent->heap_idx].val : 0;

            if (!fw_u32(f, (uint32_t)ent->key.size())) return false;
            if (!fw_u32(f, (uint32_t)ent->str.size())) return false;
            if (!fw_u64(f, exp))                        return false;
            if (!fw_str(f, ent->key.data(), (uint32_t)ent->key.size())) return false;
            if (!fw_str(f, ent->str.data(), (uint32_t)ent->str.size())) return false;
        }
    }
    return true;
}

static bool save_zsets(HTab* tab, FILE* f, std::vector<HeapItem>& heap) {
    if (tab->mask == 0) return true;
    for (size_t i = 0; i <= tab->mask; i++) {
        for (HNode* n = tab->tab[i]; n; n = n->next) {
            Entry* ent = container_of(n, Entry, node);
            if (ent->type != T_ZSET) continue;

            uint64_t exp      = (ent->heap_idx != (size_t)-1) ? heap[ent->heap_idx].val : 0;
            uint32_t n_members = (uint32_t)hm_size(&ent->zset.hmap);

            if (!fw_u32(f, (uint32_t)ent->key.size())) return false;
            if (!fw_u32(f, n_members))                  return false;
            if (!fw_u64(f, exp))                        return false;
            if (!fw_str(f, ent->key.data(), (uint32_t)ent->key.size())) return false;

            // Iterate sorted members via the AVL tree (same traversal as ZQUERY)
            uint32_t written = 0;
            ZNode* zn = zset_seekge(&ent->zset, -INFINITY, "", 0);
            while (zn) {
                if (!fw_u32(f, (uint32_t)zn->len)) return false;
                if (!fw_dbl(f, zn->score))          return false;
                if (!fw_str(f, zn->name, (uint32_t)zn->len)) return false;
                zn = znode_offset(zn, +1);
                written++;
            }
            assert(written == n_members);  
        }
    }
    return true;
}

bool persist_save(HMap* db, std::vector<HeapItem>& heap) {
    FILE* f = fopen("dump.rdb.tmp", "wb");
    if (!f) return false;

    uint32_t n_str = 0, n_zset = 0;
    count_entries(db, n_str, n_zset);

    // File header
    if (!fw_u32(f, Persist::MAGIC))   goto fail;
    if (!fw_u32(f, Persist::VERSION)) goto fail;
    if (!fw_u32(f, 2))                goto fail; 

    // Section 1: strings
    if (!fw_u32(f, (uint32_t)SectionType::STRINGS)) goto fail;
    if (!fw_u32(f, n_str))                           goto fail;
    if (!save_strings(&db->newer, f, heap))          goto fail;
    if (!save_strings(&db->older, f, heap))          goto fail;

    // Section 2: zsets
    if (!fw_u32(f, (uint32_t)SectionType::ZSETS)) goto fail;
    if (!fw_u32(f, n_zset))                        goto fail;
    if (!save_zsets(&db->newer, f, heap))          goto fail;
    if (!save_zsets(&db->older, f, heap))          goto fail;

    fclose(f);
    return rename("dump.rdb.tmp", "dump.rdb") == 0;

fail:
    fclose(f);
    return false;
}

// ──────────────────────────────────────────────────────
// Load
// ──────────────────────────────────────────────────────

static bool load_strings(FILE* f, uint32_t n, HMap* db, std::vector<HeapItem>& heap) {
    uint64_t now = persist_now_ms();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t key_len, val_len;
        uint64_t expire_at;
        if (!fr_u32(f, key_len) || !fr_u32(f, val_len) || !fr_u64(f, expire_at)) return false;

        // Expired still need to advance the file cursor.
        if (expire_at != 0 && expire_at <= now) {
            if (fseek(f, (long)(key_len + val_len), SEEK_CUR) != 0) return false;
            continue;
        }

        std::string key, val;
        if (!fr_str(f, key, key_len) || !fr_str(f, val, val_len)) return false;

        Entry* ent       = new Entry();
        ent->type        = T_STR;
        ent->key         = std::move(key);
        ent->node.hcode  = str_hash(reinterpret_cast<const uint8_t*>(ent->key.data()), ent->key.size());
        ent->str         = std::move(val);
        hm_insert(db, &ent->node);

        if (expire_at != 0) {
            persist_heap_insert(heap, ent, expire_at);
        }
    }
    return true;
}

static bool load_zsets(FILE* f, uint32_t n, HMap* db, std::vector<HeapItem>& heap) {
    uint64_t now = persist_now_ms();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t key_len, n_members;
        uint64_t expire_at;
        if (!fr_u32(f, key_len) || !fr_u32(f, n_members) || !fr_u64(f, expire_at)) return false;

        std::string key;
        if (!fr_str(f, key, key_len)) return false;

        bool expired = (expire_at != 0 && expire_at <= now);

        
        Entry* ent      = new Entry();
        ent->type       = T_ZSET;
        ent->key        = key;
        ent->node.hcode = str_hash(reinterpret_cast<const uint8_t*>(ent->key.data()), ent->key.size());

        if (!expired) {
            hm_insert(db, &ent->node);
        }

        bool ok = true;
        for (uint32_t j = 0; j < n_members; j++) {
            uint32_t name_len;
            double score;
            std::string name;
            if (!fr_u32(f, name_len) || !fr_dbl(f, score) || !fr_str(f, name, name_len)) {
                ok = false;
                break;
            }
            if (!expired) {
                zset_insert(&ent->zset, name.data(), name.size(), score);
            }
        }

        if (!ok) {
            if (expired) delete ent;
            return false;
        }

        if (expired) {
            delete ent;
        } else if (expire_at != 0) {
            persist_heap_insert(heap, ent, expire_at);
        }
    }
    return true;
}

bool persist_load(HMap* db, std::vector<HeapItem>& heap) {
    FILE* f = fopen("dump.rdb", "rb");
    if (!f) return true;

    uint32_t magic, version, n_sections;
    if (!fr_u32(f, magic)   || magic   != Persist::MAGIC)   goto fail;
    if (!fr_u32(f, version) || version != Persist::VERSION) goto fail;
    if (!fr_u32(f, n_sections))                              goto fail;

    for (uint32_t s = 0; s < n_sections; s++) {
        uint32_t type, n_records;
        if (!fr_u32(f, type) || !fr_u32(f, n_records)) goto fail;

        switch ((SectionType)type) {
        case SectionType::STRINGS:
            if (!load_strings(f, n_records, db, heap)) goto fail;
            break;
        case SectionType::ZSETS:
            if (!load_zsets(f, n_records, db, heap)) goto fail;
            break;
        default:
            fprintf(stderr, "persist_load: unknown section type %u\n", type);
            goto fail;
        }
    }

    fclose(f);
    return true;

fail:
    fclose(f);
    return false;
}