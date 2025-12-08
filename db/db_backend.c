//
// Created by zzz32 on 10/5/2025.
//

#include "db_backend.h"
#include "../logger.h"
#include "../util/config_extend.h"
#include <sys/mman.h>
#include <stdio.h>

void db_print_internal(RepaDB *db);

DBActionStatus db_set__update_inline_value(RepaDB *db, DBIndex *index, GString *value);

DBActionStatus db_set__append_value(RepaDB *db, GString *key, GString *value);

DBActionStatus internal__db_del(RepaDB *db, GString *key, bool lock, bool create_free_block, bool do_not_lock_index_rwlock);

RepaDB *master_db;

RepaDB *get_master_db() {
    return master_db;
}

gint compare_offset(gconstpointer a, gconstpointer b, gpointer userdata) {
    guint ka = GPOINTER_TO_SIZE(a);
    guint kb = GPOINTER_TO_SIZE(b);

    if (ka < kb) return -1;
    if (ka > kb) return 1;
    return 0;
}

RepaDB *db_init(size_t size) {
    RepaDB *db = g_new0(RepaDB, 1);
    db->data = mmap(
            NULL,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
    );
    db->allocated_size = size;

    db->index = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    cor_rwlock_init(&db->index_rwlock);

    db->index_area = g_array_new(FALSE, TRUE, sizeof(DBIndex *));
    cor_rwlock_init(&db->index_area_rwlock);


    DBIndex *null_index = g_new0(DBIndex, 1);
    g_array_append_val(db->index_area, null_index);

    db->free_index = g_tree_new_full((GCompareDataFunc) compare_offset, NULL, NULL, NULL);
    cor_rwlock_init(&db->free_index_rwlock);


    db->empty_indexes = g_array_new(FALSE, TRUE, sizeof(uint));
    cor_rwlock_init(&db->empty_indexes_rwlock);

    db->ordered_index = g_tree_new_full((GCompareDataFunc) compare_offset, NULL, NULL, NULL);

    return db;
}

void free_index(DBIndex* index){
    g_free(index->name);
    g_free(index);
}
gboolean free_values_cb(gpointer key, gpointer value, gpointer user_data)
{
    if (value) {
        free(value);
    }
    return FALSE;
}

void db_free(RepaDB *db) {
    if (db->data != MAP_FAILED) {
        if (munmap(db->data, db->allocated_size) == -1) {
            LOG(LOG_ERROR, "While munmap");
        }
        db->data = NULL;
    }

    g_tree_foreach(db->free_index, free_values_cb, NULL);
    g_tree_destroy(db->free_index);

    for (int i = 0; i < db->index_area->len; ++i) {
        free_index(g_array_index(db->index_area, DBIndex*, i));
    }
    g_array_free(db->index_area, TRUE);

    g_hash_table_destroy(db->index);
    g_tree_destroy(db->ordered_index);
    g_array_free(db->empty_indexes, TRUE);

    g_free(db);
}

void init_master_db() {
    size_t len = 512ULL * 1024 * 1024; // 512 MB
    len = get_int64_config_var_or_default("max_memory_byte", 1024, true);
    //len = 32;

    LOG(LOG_INFO, "BD started with %lld bytes capacity", len);

    master_db = db_init(len);

    db_print_internal(master_db);
}

void stop_master_db() {
    LOG(LOG_INFO, "Stopping master db");
    db_free(get_master_db());
}

bool proceed_replacement(RepaDB *db, ssize_t *new_offset, ssize_t needed_size, DBIndex* skip, bool* was_skip) {
    cor_rwlock_wrlock(&db->free_index_rwlock);
    cor_rwlock_wrlock(&db->index_rwlock);
    cor_rwlock_wrlock(&db->index_area_rwlock);
    cor_rwlock_wrlock(&db->empty_indexes_rwlock);

    ssize_t begin_offset = 0;
    ssize_t current_offset = begin_offset;
    ssize_t founded_size = 0;
    while (1) {
        DBFreeBlock *free_block = g_tree_lookup(db->free_index, GINT_TO_POINTER(current_offset));
        DBIndex *index_block = g_tree_lookup(db->ordered_index, GINT_TO_POINTER(current_offset));
        if (free_block && index_block) {
            LOG(LOG_ERROR, "Block overlap");
        }
        if (!free_block && !index_block) {
            ssize_t new_top = begin_offset + needed_size;
            if(current_offset == db->used_top && new_top < db->allocated_size){
                db->used_top = new_top;
                founded_size = needed_size;
            }
            else{
                LOG(LOG_ERROR, "Next block not found or overflow");
                goto cleanup;
            }
        }
        ssize_t prev_begin = current_offset;
        if (free_block) {
            current_offset += free_block->size;
            founded_size += free_block->size;
            //LOG(LOG_INFO, "Alloc at %ld len %ld", free_block->offset, free_block->size);
            g_tree_remove(db->free_index, GSIZE_TO_POINTER(free_block->offset));
            g_free(free_block);
        }
        if (index_block) {
            current_offset += index_block->size;
            founded_size += index_block->size;
            GString key = {.str = index_block->name, .len = strlen(index_block->name), .allocated_len = 0};
            if(index_block != skip){
                //LOG(LOG_INFO, "Deleting '%s' at %ld", index_block->name, index_block->offset);
                internal__db_del(db, &key, false, false, true);
            }
            else{
                *was_skip = TRUE;
            }
        }
        if (founded_size >= needed_size) {
            *new_offset = begin_offset;
            ssize_t end_needed = current_offset - (founded_size - needed_size);
            ssize_t end_founded = begin_offset + founded_size;
            if (end_founded > end_needed) {
                DBFreeBlock *new_free_block = g_new0(DBFreeBlock, 1);
                new_free_block->offset = end_needed;
                new_free_block->size = end_founded - end_needed;
                g_tree_insert(db->free_index, GSIZE_TO_POINTER(new_free_block->offset), new_free_block);
            }
            break;
        }
    }

    cleanup:

    cor_rwlock_unlock(&db->free_index_rwlock);
    cor_rwlock_unlock(&db->index_rwlock);
    cor_rwlock_unlock(&db->index_area_rwlock);
    cor_rwlock_unlock(&db->empty_indexes_rwlock);

    return founded_size > 0;
}

typedef struct s_join_free_segments_data {
    DBFreeBlock *first_in_new;
    GArray *to_remove;
} JoinFreeSegmentsData;


gboolean internal__join_free_segments_foreach(gpointer key, gpointer value, gpointer raw_user_data) {
    DBFreeBlock *block = value;
    JoinFreeSegmentsData *userdata = raw_user_data;

    //LOG(LOG_INFO, "[%d; %d]", (int) block->offset, (int) block->size);

    if (userdata->first_in_new == NULL) {
        userdata->first_in_new = block;
    } else {
        if (block->offset == userdata->first_in_new->offset + userdata->first_in_new->size) {
            //joining
            userdata->first_in_new->size += block->size;
            g_array_append_val(userdata->to_remove, key);
        } else {
            userdata->first_in_new = block;
        }
    }

    return FALSE;
}


void db_service_join_free_segments(RepaDB *db) {

    JoinFreeSegmentsData *userdata = g_new0(JoinFreeSegmentsData, 1);
    userdata->to_remove = g_array_new(FALSE, FALSE, sizeof(void *));

    cor_rwlock_wrlock(&db->free_index_rwlock);

    g_tree_foreach(db->free_index, internal__join_free_segments_foreach, userdata);

    for (int i = 0; i < userdata->to_remove->len; ++i) {
        g_tree_remove(db->free_index, g_array_index(userdata->to_remove, gpointer, i));
    }

    cor_rwlock_unlock(&db->free_index_rwlock);

    g_array_free(userdata->to_remove, TRUE);
    g_free(userdata);

    //db_print_internal(db);
}

void db_write_lock(RepaDB *db, GString *key) {
    cor_rwlock_rdlock(&db->index_area_rwlock);
    cor_rwlock_rdlock(&db->index_rwlock);
    guint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));
    DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);
    cor_rwlock_wrlock(&index->rwlock);
    cor_rwlock_unlock(&db->index_rwlock);
    cor_rwlock_unlock(&db->index_area_rwlock);
}

void db_unlock(RepaDB *db, GString *key) {
    cor_rwlock_rdlock(&db->index_area_rwlock);
    cor_rwlock_rdlock(&db->index_rwlock);
    guint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));
    DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);
    cor_rwlock_unlock(&index->rwlock);
    cor_rwlock_unlock(&db->index_rwlock);
    cor_rwlock_unlock(&db->index_area_rwlock);
}

DBActionStatus db_set(RepaDB *db, GString *key, GString *value) {
    cor_rwlock_rdlock(&db->index_area_rwlock);
    cor_rwlock_rdlock(&db->index_rwlock);
    guint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));
    cor_rwlock_unlock(&db->index_rwlock);

    DBActionStatus alloc_status;

    if (index_pos) {
        DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);

        cor_rwlock_unlock(&db->index_area_rwlock);
        alloc_status = db_set__update_inline_value(db, index, value);
    } else {
        cor_rwlock_unlock(&db->index_area_rwlock);
        alloc_status = db_set__append_value(db, key, value);
    }

    db_print_internal(db);
    //db__check_free_index_consistency(db->free_index);
    return alloc_status;
}

GString *db_get(RepaDB *db, GString *key) {
    cor_rwlock_rdlock(&db->index_rwlock);
    uint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));
    cor_rwlock_unlock(&db->index_rwlock);

    if (index_pos == 0) {
        return NULL;
    }

    cor_rwlock_rdlock(&db->index_area_rwlock);
    DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);
    cor_rwlock_unlock(&db->index_area_rwlock);

    GString *result = g_string_sized_new((ssize_t) index->size);

    cor_rwlock_rdlock(&index->rwlock);
    g_string_append_len(result, db->data + index->offset, index->size);
    cor_rwlock_unlock(&index->rwlock);

    db_print_internal(db);

    return result;
}


bool search_extend_free_space(RepaDB *db, ssize_t start_offset, ssize_t needed_size, bool allocate) {
    ssize_t founded_size = 0;
    cor_rwlock_wrlock(&db->free_index_rwlock);
    while (TRUE) {
        ssize_t next_section_offset = start_offset + founded_size;
        DBFreeBlock *free_block = g_tree_lookup(db->free_index, GSIZE_TO_POINTER(next_section_offset));
        if (free_block == NULL) {
            break;
        }

        founded_size += free_block->size;

        if (allocate) {
            g_tree_remove(db->free_index, GSIZE_TO_POINTER(free_block->offset));
            free(free_block);
        }

        if (founded_size >= needed_size) {
            break;
        }
    }

    if (founded_size < needed_size) {
        cor_rwlock_unlock(&db->free_index_rwlock);
        return FALSE;
    }
    if (founded_size == needed_size) {
        cor_rwlock_unlock(&db->free_index_rwlock);
        return TRUE;
    }

    if (allocate && founded_size > needed_size) {
        ssize_t rest_section_offset = start_offset + needed_size;
        ssize_t rest_section_size = founded_size - needed_size;

        DBFreeBlock *free_block = g_new0(DBFreeBlock, 1);
        free_block->offset = rest_section_offset;
        free_block->size = rest_section_size;

        g_tree_insert(db->free_index, GSIZE_TO_POINTER(rest_section_offset), free_block);
    }
    cor_rwlock_unlock(&db->free_index_rwlock);

    return TRUE;
}

struct s_internal__free_block_foreach_user_data {
    size_t needed_size;
    DBFreeBlock *founded_block;
};

gboolean internal__free_block_foreach(gpointer key, gpointer value, gpointer raw_user_data) {
    DBFreeBlock *block = value;
    struct s_internal__free_block_foreach_user_data *userdata = raw_user_data;
    if (block->size >= userdata->needed_size) {
        userdata->founded_block = block;
        return TRUE;
    }
    return FALSE;
}

size_t search_any_free_space(RepaDB *db, ssize_t needed_size, bool allocate, ssize_t *new_offset,
                      DBIndex *looking_for_index, bool* is_index_included) {

    *is_index_included = FALSE;

    if(needed_size > db->allocated_size){
        return FALSE;
    }

    struct s_internal__free_block_foreach_user_data *userdata = g_new0(struct s_internal__free_block_foreach_user_data,
                                                                       1);

    userdata->needed_size = needed_size;

    cor_rwlock_wrlock(&db->free_index_rwlock);

    g_tree_foreach(db->free_index, internal__free_block_foreach, userdata);
    if (userdata->founded_block != NULL) {
        if (allocate) {
            g_tree_remove(db->free_index, GSIZE_TO_POINTER(userdata->founded_block->offset));
            *new_offset = userdata->founded_block->offset;

            userdata->founded_block->offset = *new_offset + needed_size;
            userdata->founded_block->size = userdata->founded_block->size - needed_size;

            g_assert(userdata->founded_block->size >= 0);

            if (userdata->founded_block->size == 0) {
                g_free(userdata->founded_block);
            } else if (userdata->founded_block->size > 0) {
                g_tree_insert(db->free_index, GSIZE_TO_POINTER(userdata->founded_block->offset),
                              userdata->founded_block);
            }
        }

        cor_rwlock_unlock(&db->free_index_rwlock);
        g_free(userdata);
        return TRUE;
    }

    if (db->used_top + needed_size <= db->allocated_size) {
        *new_offset = (ssize_t) db->used_top;
        db->used_top += needed_size;

        cor_rwlock_unlock(&db->free_index_rwlock);
        g_free(userdata);
        return TRUE;
    }

    cor_rwlock_unlock(&db->free_index_rwlock);

    g_free(userdata);


    if (allocate && proceed_replacement(db, new_offset, needed_size, looking_for_index, is_index_included)) {
        return TRUE;
    }

    *new_offset = -1;
    return FALSE;
}


DBActionStatus db_set__append_value(RepaDB *db, GString *key, GString *value) {
    DBIndex *index = g_new0(DBIndex, 1);
    char *name_for_index = g_strdup(key->str);

    index->name = name_for_index;
    index->size = (ssize_t) value->len;
    index->offset = 0;
    index->timestamp = time(NULL);
    index->ttl = -1;
    cor_rwlock_init(&index->rwlock);


    bool _;
    if (search_any_free_space(db, index->size, TRUE, &index->offset, index, &_)) {
        memcpy(
                db->data + index->offset,
                value->str,
                index->size
        );
    } else {
        g_free(index);
        return DB_NOT_ENOUGH_SPACE;
    }

    uint index_pos = 0;

    cor_rwlock_wrlock(&db->index_area_rwlock);
    cor_rwlock_wrlock(&db->index_rwlock);
    cor_rwlock_wrlock(&db->empty_indexes_rwlock);

    if (db->empty_indexes->len > 0) {
        index_pos = g_array_index(db->empty_indexes, uint, 0);
        g_array_remove_index_fast(db->empty_indexes, 0);

        g_array_index(db->index_area, DBIndex*, index_pos) = index;
    } else {
        g_array_append_val(db->index_area, index);
        index_pos = db->index_area->len - 1;
    }
    cor_rwlock_unlock(&db->index_area_rwlock);
    cor_rwlock_unlock(&db->empty_indexes_rwlock);



    uint existed_index = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, name_for_index));
    if(existed_index != 0){
        LOG(LOG_WARNING, "Double set detected");
        cor_rwlock_unlock(&db->index_rwlock);
        internal__db_del(db, key, true, true, false);
        cor_rwlock_wrlock(&db->index_rwlock);
    }

    g_hash_table_insert(db->index, name_for_index, GUINT_TO_POINTER(index_pos));
    g_tree_insert(db->ordered_index, GSIZE_TO_POINTER(index->offset), index);
    cor_rwlock_unlock(&db->index_rwlock);

    return DB_OK;
}

DBActionStatus db_set__update_inline_value(RepaDB *db, DBIndex *index, GString *value) {
    cor_rwlock_rdlock(&index->rwlock);
    ssize_t prev_size = index->size;
    cor_rwlock_unlock(&index->rwlock);
    ssize_t new_size = (ssize_t) value->len;

    ssize_t new_offset = -1;

    bool free_prev_segment = FALSE;


    cor_rwlock_wrlock(&index->rwlock);

    if (new_size == prev_size) {
        new_offset = (ssize_t) index->offset;
    } else if (new_size < prev_size) {

        ssize_t new_end = index->offset + new_size;
        ssize_t free_section_size = prev_size - new_size;

        DBFreeBlock *free_block = g_new0(DBFreeBlock, 1);
        free_block->offset = new_end;
        free_block->size = free_section_size;

        cor_rwlock_wrlock(&db->free_index_rwlock);
        g_tree_insert(db->free_index, GSIZE_TO_POINTER(new_end), free_block);
        cor_rwlock_unlock(&db->free_index_rwlock);


        new_offset = (ssize_t) index->offset;
    } else { // new_size > prev_size
        ssize_t needed_size = new_size - prev_size;
        if (search_extend_free_space(db, index->offset + index->size, needed_size, TRUE)) {
            new_offset = (ssize_t) index->offset;
        } else if (search_any_free_space(db, new_size, TRUE, &new_offset, index, &free_prev_segment)) {
            free_prev_segment = !free_prev_segment;
        }
    }

    if (new_offset == -1) {
        return DB_NOT_ENOUGH_SPACE;
    }

    // copy new value
    memcpy(
            db->data + new_offset,
            value->str,
            new_size
    );

    //free prev value
    if (free_prev_segment) {
        DBFreeBlock *free_block = g_new0(DBFreeBlock, 1);
        free_block->offset = index->offset;
        free_block->size = index->size;

        cor_rwlock_wrlock(&db->free_index_rwlock);
        g_tree_insert(db->free_index, GSIZE_TO_POINTER(free_block->offset), free_block);
        cor_rwlock_unlock(&db->free_index_rwlock);
    }

    //update index
    cor_rwlock_wrlock(&db->index_rwlock);
    g_tree_remove(db->ordered_index, GSIZE_TO_POINTER(index->offset));
    index->offset = new_offset;
    index->size = new_size;
    index->timestamp = time(NULL);
    g_tree_insert(db->ordered_index, GSIZE_TO_POINTER(index->offset), index);
    cor_rwlock_unlock(&db->index_rwlock);


    cor_rwlock_unlock(&index->rwlock);

    return DB_OK;
}

ssize_t db_get_value_size(RepaDB *db, GString *key) {
    cor_rwlock_rdlock(&db->index_rwlock);
    uint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));
    cor_rwlock_unlock(&db->index_rwlock);

    if (index_pos == 0) {
        return -1;
    }

    cor_rwlock_rdlock(&db->index_area_rwlock);
    DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);
    cor_rwlock_unlock(&db->index_area_rwlock);

    return index->size;
}


#define IF_LOCK if(lock)

DBActionStatus internal__db_del(RepaDB *db, GString *key, bool lock, bool create_free_block, bool do_not_lock_index_rwlock) {
    IF_LOCK if(!do_not_lock_index_rwlock) cor_rwlock_wrlock(&db->index_rwlock);
    uint index_pos = GPOINTER_TO_UINT(g_hash_table_lookup(db->index, key->str));

    if (index_pos == 0) {
        IF_LOCK if(!do_not_lock_index_rwlock) cor_rwlock_unlock(&db->index_rwlock);
        return DB_KEY_NOT_FOUND;
    }
    g_hash_table_remove(db->index, key->str);


    IF_LOCK cor_rwlock_wrlock(&db->index_area_rwlock);
    DBIndex *index = g_array_index(db->index_area, DBIndex*, index_pos);
    g_tree_remove(db->ordered_index, GSIZE_TO_POINTER(index->offset));

    IF_LOCK if(!do_not_lock_index_rwlock) cor_rwlock_unlock(&db->index_rwlock);


    IF_LOCK cor_rwlock_wrlock(&db->empty_indexes_rwlock);
    g_array_append_val(db->empty_indexes, index_pos);
    IF_LOCK cor_rwlock_unlock(&db->empty_indexes_rwlock);

    if (create_free_block) {
        DBFreeBlock *free_block = g_new0(DBFreeBlock, 1);
        free_block->offset = index->offset;
        free_block->size = index->size;

        IF_LOCK cor_rwlock_wrlock(&db->free_index_rwlock);
        g_tree_insert(db->free_index, GSIZE_TO_POINTER(free_block->offset), free_block);
        IF_LOCK cor_rwlock_unlock(&db->free_index_rwlock);
    }

    free_index(index);
    g_array_index(db->index_area, DBIndex*, index_pos) = NULL;
    IF_LOCK cor_rwlock_unlock(&db->index_area_rwlock);

    db_print_internal(db);

    //db__check_free_index_consistency(db->free_index);
    return DB_OK;
}

DBActionStatus db_del(RepaDB *db, GString *key) {
    return internal__db_del(db, key, true, true, false);
}


typedef struct s_test_print_internal {
    char value;
    bool is_alloc;
    bool is_free;
    bool is_last;
    ssize_t meta_index;
} TestPrintElement;



#define look_size 32

gboolean internal__print_free_block_foreach(gpointer key, gpointer value, gpointer raw_user_data) {



    DBFreeBlock *block = value;
    TestPrintElement *bitmap = raw_user_data;

    for (ssize_t i = block->offset; i < block->offset + block->size; ++i) {
        if (i >= look_size) {
            break;
        }

        bitmap[i].is_free = true;
        bitmap[i].meta_index = block->offset;
    }

    return FALSE;
}

void db_print_internal(RepaDB *db) {
    return;

    cor_rwlock_rdlock(&db->free_index_rwlock);
    cor_rwlock_rdlock(&db->index_rwlock);
    cor_rwlock_rdlock(&db->index_area_rwlock);
    cor_rwlock_rdlock(&db->empty_indexes_rwlock);

    TestPrintElement *bitmap = g_new0(TestPrintElement, look_size);

    for (int i = 0; i < look_size; ++i) {
        bitmap[i].value = ((char *) db->data)[i];
    }

    for (int i = 0; i < db->index_area->len; ++i) {
        DBIndex *index = g_array_index(db->index_area, DBIndex*, i);
        if (index == NULL) {
            continue;
        }

        for (size_t j = (size_t) index->offset; j < index->offset + index->size; ++j) {
            if (j >= look_size) {
                break;
            }

            if (bitmap[j].is_alloc) {
                LOG(LOG_ERROR, "DOUBLE ALLOC");
            }
            bitmap[j].is_alloc = true;
            bitmap[j].meta_index = (int) index->offset;
        }
    }

    g_tree_foreach(db->free_index, internal__print_free_block_foreach, bitmap);

    if(db->used_top < look_size)
        bitmap[db->used_top].is_last = true;

    // PRINTING
    printf("\n");

    for (int i = 0; i < look_size; ++i) {

        int counter = 0;
        char flag = '.';
        if (bitmap[i].is_alloc) {
            flag = '+';
            counter++;
        }
        if (bitmap[i].is_free) {
            flag = '-';
            counter++;
        }

        if (counter > 1) {
            LOG(LOG_ERROR, "ALLOC AND FREE");
        }

        printf("%c%c ", bitmap[i].value, flag);
    }
    printf("\n");
    for (int i = 0; i < look_size; ++i) {
        printf("%02ld ", bitmap[i].meta_index);
    }
    printf("\n");

    fflush(stdout);

    cor_rwlock_unlock(&db->free_index_rwlock);
    cor_rwlock_unlock(&db->index_rwlock);
    cor_rwlock_unlock(&db->index_area_rwlock);
    cor_rwlock_unlock(&db->empty_indexes_rwlock);
}