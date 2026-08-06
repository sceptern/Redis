#include "persist.h"
#include "types.h"
#include <string.h>

bool h_foreach(HTab *htab, FILE* fptr);

bool hm_foreach(HMap* hmap, FILE* fptr) {
        return h_foreach(&hmap->newer, fptr) && h_foreach(&hmap->older, fptr);
}

bool h_foreach(HTab *htab, FILE* fptr) {
    for (size_t i = 0; htab->mask!=0 && i <= htab->mask; i++) {
        for (HNode* node = htab->tab[i]; node != NULL; node = node->next) {
            Entry* ent = container_of(node, struct Entry, node);
            if (ent->type != T_STR) continue;
            
            const std::string& key = ent->key;
            const std::string& str = ent->str;
            StringRecord record{};
            record.key_len = key.size();
            record.val_len = str.size();

            if (fwrite(&record, sizeof(StringRecord), 1, fptr)!=1) { fclose(fptr); return false; }
            if (fwrite(key.c_str(), key.size(), 1, fptr)!=1) { fclose(fptr); return false; }
            if (fwrite(str.c_str(), str.size(), 1, fptr)!=1) { fclose(fptr); return false; }
        }
    }
    return true;
}

bool persist_save(HMap* db) {
    return persist_save_hmap(db);
}

bool persist_load(HMap* db) {
    return persist_load_hmap(db);
}

bool persist_save_hmap(HMap* db) {
    FILE* fptr = fopen("dump.rdb.tmp", "wb");
    if (!fptr) return false;

    FileHeader header{};
    header.magic = 0x52444231;
    header.version = 1;
    header.n_sections = 1;

    SectionHeader sh{};
    sh.type = static_cast<uint32_t>(SectionType::STRINGS);
    sh.n_records = db->newer.size + db->older.size;

    // Writing header
    if (fwrite(&header.magic, sizeof(header.magic), 1, fptr)!=1) { fclose(fptr); return false; }
    if (fwrite(&header.version, sizeof(header.version), 1, fptr)!=1) { fclose(fptr); return false; }
    if (fwrite(&header.n_sections, sizeof(header.n_sections), 1, fptr)!=1) { fclose(fptr); return false; }
    
    // Writing Section Header
    if (fwrite(&sh.type, sizeof(sh.type), 1, fptr)!=1) { fclose(fptr); return false; }
    if (fwrite(&sh.n_records, sizeof(sh.n_records), 1, fptr)!=1) { fclose(fptr); return false; }


    
    if (hm_foreach(db, fptr)!= true) { fclose(fptr); return false; }
    fclose(fptr);
    if (rename("dump.rdb.tmp", "dump.rdb")!=0) return false;
    return true;
}

bool persist_load_hmap(HMap* db) {
    FILE* fptr = fopen("dump.rdb", "rb");
    if (!fptr) return true;

    FileHeader header{};
    if (fread(&header, sizeof(header), 1, fptr) != 1) return false;
    if (header.magic != Persist::MAGIC) return false;
    if (header.version != Persist::VERSION) return false;

    SectionHeader sh{};
    if (fread(&sh, sizeof(sh), 1, fptr)!= 1) return false;

    for (size_t i = 0; i < sh.n_records; i++) {
        StringRecord record{};
        if (fread(&record, sizeof(record), 1, fptr) != 1) return false;
        
        char* key = new char[record.key_len + 1];
        key[record.key_len] = '\0';
        if (fread(key, 1, record.key_len, fptr) != record.key_len) {
            delete[] key;
            return false;
        }

        char* str = new char[record.val_len + 1];
        str[record.val_len] = '\0';
        if (fread(str, 1, record.val_len, fptr) != record.val_len) {
            delete[] str;
            delete[] key;
            return false;
        }

        Entry* ent = new Entry();
        ent->type = T_STR;
        ent->key = key;
        ent->node.hcode = str_hash(
            reinterpret_cast<const uint8_t*>(ent->key.data()),
            ent->key.size()
        );
        ent->str = str;

        hm_insert(db, &ent->node);
        delete[] key;
        delete[] str;
    }

    fclose(fptr);
    return true;
}