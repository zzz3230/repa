//
// Created by zzz32 on 9/29/2025.
//

#include "command_processor.h"
#include "../resp_serializer.h"
#include "db_backend.h"
#include "stats_service.h"
#include "../util/config_extend.h"
#include "../repa_client.h"


gint g_strcasecmp0 (const char *str1, const char *str2)
{
    if (str1 == str2)
        return 0;
    if (str1 == NULL)
        return -1;
    if (str2 == NULL)
        return 1;

    return g_ascii_strcasecmp(str1, str2);
}

bool cmd_set(GString* key, GString* value, char** error_message);
GString* cmd_get(GString* key, char** error_message);
bool cmd_del(GString* key, char** error_message);
void cmd_join();

bool command_is(RespData* command, char* name){
    if(command->type == RESP_TYPE_ARRAY && command->data.array_val->len > 0){
        RespData* first = g_array_index(command->data.array_val, RespData*, 0);
        if(first->type == RESP_TYPE_BULK_STRING || first->type == RESP_TYPE_STRING){
            if(g_strcasecmp0(first->data.string_val->str, name) == 0){
                return TRUE;
            }
        }
    }
    return FALSE;
}

GString* get_second_string_arg(RespData* command){
    if(command->type == RESP_TYPE_ARRAY && command->data.array_val->len > 1){
        RespData* second = g_array_index(command->data.array_val, RespData*, 1);
        if(second->type == RESP_TYPE_BULK_STRING || second->type == RESP_TYPE_STRING){
            return second->data.string_val;
        }
    }
    return NULL;
}
GString* get_third_string_arg(RespData* command){
    if(command->type == RESP_TYPE_ARRAY && command->data.array_val->len > 2){
        RespData* third = g_array_index(command->data.array_val, RespData*, 2);
        if(third->type == RESP_TYPE_BULK_STRING || third->type == RESP_TYPE_STRING){
            return third->data.string_val;
        }
    }
    return NULL;
}

RespData* make_error(char* message){
    return resp_data_from_error(g_string_new(message));
}
RespData* make_ok(){
    return resp_data_from_simple_string(g_string_new("OK"));
}

#define SKIP_AUTH true
#define CHECK_AUTH if(!client->authenticated && !SKIP_AUTH) { return make_error("Authentication required"); }

RespData* process_command(RespData* command, RepaClient* client){
    stats_add(STATS_TOTAL_COMMANDS, 1);

    if(command_is(command, "sleep")){
        g_usleep(5*1000000);
        return make_ok();
    }

    if(command_is(command, "set")){ CHECK_AUTH

        stats_add(STATS_CMD_SET, 1);
        GString* key = get_second_string_arg(command);
        GString* value = get_third_string_arg(command);
        if(!key || !value){
            return make_error("Invalid arguments");
        }
        char* error_message = NULL;
        if(cmd_set(key, value, &error_message)){
            return make_ok();
        }
        else{
            return make_error(error_message);
        }
    }

    if(command_is(command, "get")){ CHECK_AUTH
        stats_add(STATS_CMD_GET, 1);
        GString* key = get_second_string_arg(command);
        if(!key){
            return make_error("Invalid arguments: no key");
        }
        char* error_message = NULL;
        GString* value = cmd_get(key, &error_message);
        if(value){
            return resp_data_from_string(value);
        }
        else{
            return make_error(error_message);
        }
    }

    if(command_is(command, "del")){ CHECK_AUTH
        stats_add(STATS_CMD_DEL, 1);
        GString* key = get_second_string_arg(command);
        if(!key){
            return make_error("Invalid arguments: no key");
        }
        char* error_message = NULL;
        if(cmd_del(key, &error_message)){
            return make_ok();
        }
        else{
            return make_error(error_message);
        }
    }

    if(command_is(command, "ping")){
        return resp_data_from_simple_string(g_string_new("pong"));
    }

    if(command_is(command, "join")){
        cmd_join();
        return make_ok();
    }

    if(command_is(command, "stats")){
        char** stats_names = get_available_stats();
        int stats_count = 0;
        for(;stats_names[stats_count][0]; stats_count++);

        RespData* arr = resp_data_new_array_sized(stats_count);
        RespData* s = resp_data_from_simple_string(g_string_new("Repa statistic:"));
        g_array_append_val(arr->data.array_val, s);

        for (int i = 0; stats_names[i][0]; ++i) {
            GString* str = g_string_sized_new(32);
            g_string_append_printf(str, " - %s = %d", stats_names[i], stats_add(stats_names[i], 0));

            s = resp_data_from_simple_string(str);
            g_array_append_val(arr->data.array_val, s);
        }

        return arr;
    }

    if(command_is(command, "auth")){
        stats_add(STATS_CMD_SET, 1);
        GString* login = get_second_string_arg(command);
        GString* password = get_third_string_arg(command);
        if(!login || !password){
            return make_error("Invalid arguments");
        }
        char* def_login = get_string_config_var_or_default("default_user", NULL, FALSE);
        char* def_password = get_string_config_var_or_default("default_password", NULL, FALSE);
        if(!def_login || !def_password){
            return make_error("Set in config variables default_user and default_password");
        }
        if(strcmp(login->str, def_login) == 0 && strcmp(password->str, def_password) == 0){
            client->authenticated = TRUE;
            return make_ok();
        }
        return make_error("Invalid credentials");
    }

    char* err_msg = "ERR this is unknown command";
    return resp_data_from_error(g_string_new(err_msg));
}



// ---DATABASE---

// STD_MOVE(key)
GString* cmd_get(GString* key, char** error_message){
    //LOG(LOG_INFO, "GET '%s'", key->str);

    GString* result = db_get(get_master_db(), key);
    if(result == NULL){
        *error_message = "Key not presented";
        return NULL;
    }
    return result;
}
bool cmd_set(GString* key, GString* value, char** error_message){
    //LOG(LOG_INFO, "SET '%s'", key->str);

    DBActionStatus status = db_set(get_master_db(), key, value);
    if(status == DB_OK){
        stats_add(STATS_CMD_USED_MEM, (int)value->len);
        return TRUE;
    }
    else{
        *error_message = "Unknown error";
    }
    return FALSE;
}
bool cmd_del(GString* key, char** error_message){
    ssize_t len = db_get_value_size(get_master_db(), key);
    DBActionStatus status = db_del(get_master_db(), key);
    if(status == DB_OK){
        if(len == -1){
            LOG(LOG_ERROR, "len = -1, but DB_OK");
        }
        else{
            stats_add(STATS_CMD_USED_MEM, -(int)len);
        }
        return TRUE;
    }
    if(status == DB_KEY_NOT_FOUND){
        *error_message = "Key not found";
    }
    else{
        *error_message = "Unknown error";
    }
    return FALSE;
}

// ---CONFIG---

bool cmd_config_set(GString* key, GString* value, char** error_message){
    return TRUE;
}
GString* cmd_config_get(GString* key, char** error_message){
}

// ---UTILITY---

bool cmd_auth(GString* login, GString* password, char** error_message){
    return TRUE;
}



void cmd_join(){
    db_service_join_free_segments(get_master_db());
}