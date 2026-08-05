#include "vdb.h"
#include "common.h"
#include <assert.h>
#include <math.h>
#include <algorithm>
#include <string.h>

static uint64_t ventry_hash(const std::string &key) {
    return str_hash((uint8_t *)key.data(), key.size());
}

static bool ventry_eq(HNode* node, HNode* key) {
    VEntry* ent = container_of(node, VEntry, node);
    VEntry* k   = container_of(key,  VEntry, node);
    return ent->key == k->key;
}

float cosine_sim(const std::vector<float> &a, const std::vector<float> &b) {
    assert(a.size() == b.size());
    
    const size_t n = a .size();
    size_t i = 0;

    __m256 dot = _mm256_setzero_ps();
    __m256 na  = _mm256_setzero_ps();
    __m256 nb  = _mm256_setzero_ps();

    for (; i + 7 < n; i+=8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);

        dot = _mm256_add_ps(dot, _mm256_mul_ps(va, vb));
        na  = _mm256_add_ps(na,  _mm256_mul_ps(va, va));
        nb  = _mm256_add_ps(nb,  _mm256_mul_ps(vb, vb));
    }

    alignas(32) float dot_buf[8];
    alignas(32) float na_buf[8];
    alignas(32) float nb_buf[8];

    _mm256_store_ps(dot_buf, dot);
    _mm256_store_ps(na_buf, na);
    _mm256_store_ps(nb_buf, nb);

    float dot_sum = 0.0f;
    float na_sum  = 0.0f;
    float nb_sum  = 0.0f;

    for (int j = 0; j < 8; j++) {
        dot_sum += dot_buf[j];
        na_sum  += na_buf[j];
        nb_sum  += nb_buf[j];
    }

    for (; i < n; i++) {
        dot_sum += a[i] * b[i];
        na_sum  += a[i] * a[i];
        nb_sum  += b[i] * b[i];
    }

    if (na_sum == 0.0f || nb_sum == 0.0f) return 0.0f;
    return dot_sum / (std::sqrt(na_sum) * std::sqrt(nb_sum));

}

bool parse_embedding(const std::string& json, std::vector<float>& out) {
    
    size_t start = json.find('[');
    size_t end   = json.find(']');
    if (start == std::string::npos || end == std::string::npos || end < start) {
        return false;
    }

    out.clear();
    size_t pos = start + 1;
    while (pos < end) {
        
        while (pos < end && (json[pos] == ',' || json[pos] == ' ')) pos++;
        if (pos >= end) break;

        
        char* endptr = nullptr;
        float val = strtof(json.c_str() + pos, &endptr);
        if (endptr == json.c_str() + pos) break; 
        out.push_back(val);
        pos = endptr - json.c_str();
    }

    return !out.empty();
}

void vdb_set(VDB *vdb, const std::string &key, const std::vector<float> &embedding) {
    
    VEntry lookup;
    lookup.key = key;
    lookup.node.hcode = ventry_hash(key);

    HNode *found = hm_lookup(&vdb->hmap, &lookup.node, &ventry_eq);
    if (found) {
        VEntry *ent = container_of(found, VEntry, node);
        ent->embedding = embedding;
        return;
    }

    
    VEntry *ent = new VEntry();
    ent->key = key;
    ent->node.hcode = ventry_hash(key);
    ent->embedding = embedding;
    hm_insert(&vdb->hmap, &ent->node);
}

bool vdb_del(VDB* vdb, const std::string& key) {
    VEntry lookup;
    lookup.key = key;
    lookup.node.hcode = ventry_hash(key);

    HNode* found = hm_delete(&vdb->hmap, &lookup.node, &ventry_eq);
    if (!found) return false;

    VEntry* ent = container_of(found, VEntry, node);
    delete ent;
    return true;
}

struct SearchCtx {
    const std::vector<float>* query;
    std::vector<VSearchResult> results;
};

static bool vsearch_cb(HNode *node, void *arg) {
    SearchCtx* ctx = (SearchCtx *)arg;
    VEntry* ent = container_of(node, VEntry, node);

    float score = cosine_sim(*ctx->query, ent->embedding);
    ctx->results.push_back({ent->key, score});
    return true; 
}

std::vector<VSearchResult> vdb_search(VDB *vdb, const std::vector<float> &query, size_t k) {
    SearchCtx ctx;
    ctx.query = &query;

    hm_foreach(&vdb->hmap, &vsearch_cb, &ctx);

    
    std::sort(ctx.results.begin(), ctx.results.end(),
        [](const VSearchResult &a, const VSearchResult &b) {
            return a.score > b.score;
        });

    
    if (ctx.results.size() > k) {
        ctx.results.resize(k);
    }
    return ctx.results;
}

