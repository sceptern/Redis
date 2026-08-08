#include "vdb.h"
#include "common.h"
#include <assert.h>
#include <math.h>
#include <algorithm>
#include <iostream>
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
    
    #if defined(__AVX2__)
        std::cout << "Using AVX2\n";
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
    #else
        std::cout << "Using scalar\n";
        float dot_sum = 0.0f;
        float na_sum  = 0.0f;
        float nb_sum  = 0.0f;

        for (size_t i = 0; i < a.size(); i++) {
            dot_sum += a[i] * b[i];
            na_sum  += a[i] * a[i];
            nb_sum  += b[i] * b[i];
        }
    #endif
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

bool vdb_set(VDB* vdb, const std::string& key, const std::vector<float>& embedding) {
    VEntry lookup;
    lookup.key = key;
    lookup.node.hcode = ventry_hash(key);

    HNode* found = hm_lookup(&vdb->hmap, &lookup.node, &ventry_eq);
    if (found) {
        VEntry *ent = container_of(found, VEntry, node);
        if (ent->type != V_INIT && ent->type != V_EMBED) {
            return false;  
        }
        ent->type = V_EMBED;
        ent->embedding = embedding;
        return true;
    }

    VEntry *ent = new VEntry();
    ent->key = key;
    ent->type = V_EMBED;
    ent->node.hcode = ventry_hash(key);
    ent->embedding = embedding;
    hm_insert(&vdb->hmap, &ent->node);
    return true;
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

bool vdb_add_chunk(VDB *vdb, const std::string &doc_name,
                    const std::string &chunk_name,
                    const std::string &text,
                    const std::vector<float> &embedding) {
    VEntry lookup;
    lookup.key = doc_name;
    lookup.node.hcode = ventry_hash(doc_name);

    HNode* found = hm_lookup(&vdb->hmap, &lookup.node, &ventry_eq);

    VEntry *ent;
    if (!found) {
        // first use: create the doc entry
        ent = new VEntry();
        ent->key = doc_name;
        ent->type = V_DOC;
        ent->node.hcode = lookup.node.hcode;
        hm_insert(&vdb->hmap, &ent->node);
    } else {
        ent = container_of(found, VEntry, node);
        if (ent->type != V_DOC) {
            return false;   // doc_name already exists as a plain VSET key
        }
    }

    // O(1) insert-or-overwrite by chunk_name
    ent->chunks[chunk_name] = VChunk{text, embedding};
    return true;
}

struct SearchCtx {
    const std::vector<float>* query;
    std::vector<VSearchResult> results;
};

static bool vsearch_cb(HNode* node, void* arg) {
    SearchCtx* ctx = (SearchCtx *)arg;
    VEntry* ent = container_of(node, VEntry, node);

    if (ent->type != V_EMBED) {
        return true;   // skip V_DOC entries — plain VSEARCH is VSET-only
    }

    float score = cosine_sim(*ctx->query, ent->embedding);
    ctx->results.push_back({ent->key, score});
    return true;
}

std::vector<VSearchResult> vdb_search(VDB* vdb, const std::vector<float>& query, size_t k) {
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

std::vector<VDocSearchResult> vdb_doc_search(VDB *vdb, const std::string &doc_name,
                                              const std::vector<float> &query, size_t k) {
    VEntry lookup;
    lookup.key = doc_name;
    lookup.node.hcode = ventry_hash(doc_name);

    HNode *found = hm_lookup(&vdb->hmap, &lookup.node, &ventry_eq);
    std::vector<VDocSearchResult> results;
    if (!found) {
        return results;   // no such doc
    }

    VEntry *ent = container_of(found, VEntry, node);
    if (ent->type != V_DOC) {
        return results;   // exists, but not a document
    }

    for (auto &kv : ent->chunks) {
        const std::string &chunk_name = kv.first;
        const VChunk &chunk = kv.second;
        float score = cosine_sim(query, chunk.embedding);
        results.push_back({chunk_name, chunk.text, score});
    }

    std::sort(results.begin(), results.end(),
        [](const VDocSearchResult &a, const VDocSearchResult &b) {
            return a.score > b.score;
        });

    if (results.size() > k) {
        results.resize(k);
    }
    return results;
}