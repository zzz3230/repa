//
// Created by zzz32 on 10/5/2025.
//

#ifndef REPA_SERVER_DB_BACKEND_H
#define REPA_SERVER_DB_BACKEND_H

#include "glib.h"

typedef enum DBActionStatus {
    DB_OK,
    DB_NOT_ENOUGH_SPACE,
    DB_KEY_NOT_FOUND
} DBActionStatus;

typedef struct s_db_free_block {
    ssize_t offset;
    ssize_t size;
} DBFreeBlock;

typedef struct s_db_index {
    ssize_t offset;
    ssize_t size;
    pthread_rwlock_t rwlock;
    char* name;
    long timestamp;
    long ttl;
} DBIndex;

typedef struct s_repa_database {
    void* data;
    size_t allocated_size;
    size_t used_top;
    GHashTable* index;
    pthread_rwlock_t index_rwlock;
    GArray* index_area;
    pthread_rwlock_t index_area_rwlock;
    GTree* free_index;
    pthread_rwlock_t free_index_rwlock;
    GArray* empty_indexes;
    pthread_rwlock_t empty_indexes_rwlock;
    GTree* ordered_index;
} RepaDB;

RepaDB* get_master_db();

void init_master_db();
void stop_master_db();
DBActionStatus db_set(RepaDB* db, GString* key, GString* value);
GString* db_get(RepaDB* db, GString* key);
DBActionStatus db_del(RepaDB* db, GString*  key);
ssize_t db_get_value_size(RepaDB* db, GString* key);
void db_service_join_free_segments(RepaDB* db);

void db_write_lock(RepaDB* db, GString* key);
void db_unlock(RepaDB* db, GString* key);

#endif //REPA_SERVER_DB_BACKEND_H
