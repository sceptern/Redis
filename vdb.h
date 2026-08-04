#pragma once

#include <string>
#include <vector>
#include "hashtable.h"

struct VEntry {
    HNode node;
    std::string key;
    std::vector<float> embedding;
};

struct VDB {
    HMap hmap;
};

void vdb_set(VDB *vdb, const std::string &key, const std::vector<float> &embedding);
bool vdb_del(VDB *vdb, const std::string &key);

struct VSearchResult {
    std::string text;
    float score;
};

std::vector<VSearchResult> vdb_search(VDB *vdb, const std::vector<float> &query, size_t k);
float cosine_sim(const std::vector<float> &a, const std::vector<float> &b);
bool parse_embedding(const std::string &json, std::vector<float> &out);