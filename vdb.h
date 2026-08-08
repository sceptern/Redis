#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#ifdef __AVX2__
#include <immintrin.h>
#endif
#include "hashtable.h"

// value types stored in the VDB hashtable
enum {
    V_INIT  = 0,
    V_EMBED = 1,    // single vector, from VSET
    V_DOC   = 2,    // a document made of named, embedded chunks, from VADD
};

// one named chunk of a document
struct VChunk {
    std::string text;
    std::vector<float> embedding;
};

// top-level entry in the vector db, keyed by name (VSET key or doc name)
struct VEntry {
    HNode node;                     // hashtable node
    std::string key;                // VSET key, or doc_name for VADD
    uint32_t type = V_INIT;

    std::vector<float> embedding;                    // valid iff type == V_EMBED
    std::unordered_map<std::string, VChunk> chunks;  // valid iff type == V_DOC, keyed by chunk_name

    VEntry() {
        embedding.reserve(384);
    }
};

struct VDB {
    HMap hmap;
};

struct VSearchResult {
    std::string text;
    float score;
};

struct VDocSearchResult {
    std::string chunk_name;
    std::string text;
    float score;
};


bool vdb_set(VDB *vdb, const std::string &key, const std::vector<float> &embedding);
bool vdb_del(VDB *vdb, const std::string &key);


bool vdb_add_chunk(VDB *vdb, const std::string &doc_name,
                    const std::string &chunk_name,
                    const std::string &text,
                    const std::vector<float> &embedding);

std::vector<VSearchResult> vdb_search(VDB *vdb, const std::vector<float> &query, size_t k);
float cosine_sim(const std::vector<float> &a, const std::vector<float> &b);
bool parse_embedding(const std::string &json, std::vector<float> &out);

std::vector<VSearchResult> vdb_search(VDB *vdb, const std::vector<float> &query, size_t k);
std::vector<VDocSearchResult> vdb_doc_search(VDB *vdb, const std::string &doc_name,
                                              const std::vector<float> &query, size_t k);
