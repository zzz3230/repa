//
// Created by zzz32 on 10/23/2025.
//

#include "stats_service.h"
#include "db_backend.h"
#include "../logger.h"

char* stats[] = {
        STATS_TOTAL_COMMANDS,
        STATS_CMD_GET,
        STATS_CMD_SET,
        STATS_CMD_DEL,
        STATS_CMD_USED_MEM,
        STATS_CMD_CUR_CONN,
        "\0"
};

GHashTable* locks;

char** get_available_stats(){
    return stats;
}

void init_stats_service(){
    locks = g_hash_table_new(g_str_hash, g_str_equal);
    for (int i = 0; stats[i][0]; ++i) {
        pthread_mutex_t* mutex = g_new0(pthread_mutex_t, 1);
        pthread_mutex_init(mutex, NULL);
        g_hash_table_insert(locks, stats[i], mutex);
    }
}
void stop_stats_service(){
    LOG(LOG_INFO, "Stopping stats service");
    g_hash_table_destroy(locks);
}

gboolean gstring_to_int_safe(GString* gstr, int* out_value) {
    if (!gstr || !gstr->str || !out_value)
        return FALSE;

    errno = 0;
    char* endptr;
    long val = strtol(gstr->str, &endptr, 10);

    if (errno != 0 || *endptr != '\0')  // ошибка конверсии
        return FALSE;

    *out_value = (int)val;
    return TRUE;
}

int db_int_add(RepaDB* db, GString* key, int delta){
    GString* str = db_get(db, key);

    if(str == NULL){
        str = g_string_sized_new(8);
        g_string_printf(str, "%d", delta);
        db_set(db, key, str);
        return delta;
    }

    int val;
    if(gstring_to_int_safe(str, &val)){
        val += delta;
        if(delta != 0){
            g_string_truncate_inline(str, 0);
            g_string_printf(str, "%d", val);
            db_set(db, key, str);
        }
        g_string_free(str, TRUE);
        return val;
    }
    else{
        LOG(LOG_ERROR, "Key not found %s", key->str);
    }
    g_string_free(str, TRUE);
    return 0;
}


int stats_add(char* name, int value){
    pthread_mutex_t* mutex = g_hash_table_lookup(locks, name);
    if(mutex == NULL){
        LOG(LOG_ERROR, "Unknown stat: '%s'", name);
    }
    else{
        pthread_mutex_lock(mutex);
    }
    GString g_name = { .str = name, .len = strlen(name), .allocated_len = 0 };
    int res = db_int_add(get_master_db(), &g_name, value);
    if(mutex != NULL) pthread_mutex_unlock(mutex);
    return res;
}
