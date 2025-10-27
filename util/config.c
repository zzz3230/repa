#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "exception.h"
#include "config_parser.h"
#include "../logger.h"
#include "utils.h"
#include "uthash/utarray.h"

bool was_created = false;

const ConfigVariable BAD_VARIABLE = {.type = UNDEFINED};
int vars_count = 0;
char* master_config_path = "repa.conf";


struct HashableConfigVariable* vars_hash_table = NULL;

bool m_is_debug_mode = false;

bool is_debug_mode(void){
    return m_is_debug_mode;
}
void set_debug_mode(bool mode){
    m_is_debug_mode = mode;
}

int create_config_table(void){
    if(was_created){
        return 1;
    }
    was_created = true;
    return 0;
}


int parse_config(const char* path) {
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return 1;
    }

    if(config_parser_parse_file(path) == 1){
        PUSH_EXCEPTION("Error while parsing config file");
        return 1;
    }

    ConfigVariable* parsed_vars;
    int parsed_vars_count;
    config_parser_get_parsed_vars(&parsed_vars, &parsed_vars_count);
    config_parser_finish_parsing_file();
    finish_parsing();

    for (int i = 0; i < parsed_vars_count; ++i) {
        if(define_variable(parsed_vars[i]) == 1){
            PUSH_EXCEPTION("Failed to define variable %s", parsed_vars[i].name);
            return 1;
        }
    }

    free(parsed_vars);

    return 0;
}

int try_parse_env_plugins(char* plugins){
    int count = 0;
    while (strsep(&plugins, ",") != NULL){
        count++;
    }

    char** split = malloc(count * sizeof(char*));
    if(split == NULL){
        PUSH_EXCEPTION("Failed to allocate memory for plugins");
        return 1;
    }

    int index = 0;

    char* token;
    while ((token = strsep(&plugins, ","))){
        int err;
        char* copy = copy_string(token, &err);
        split[index++] = copy;
        if(err != 0){
            PUSH_EXCEPTION("Failed to copy string %s", token);
            free(split);
            return 1;
        }
    }

    set_variable(
            (ConfigVariable){
                    .name = "plugins",
                    .description = "Plugins",
                    .data.string = split,
                    .type = STRING,
                    .count = 1
            }
    );

    return 0;
}

int apply_environment(void){
    char* logs_path = getenv("PROXY_LOG_PATH");
    char* plugins = getenv("PROXY_MASTER_PLUGINS");

    if(logs_path != NULL){
        set_variable(
                (ConfigVariable){
                        .name = "logs",
                        .description = "Path to the log file",
                        .data.string = alloc_string_array(1, NULL, copy_string(logs_path, NULL)),
                        .type = STRING,
                        .count = 1
                }
        );
    }
    if(plugins != NULL){
        if(try_parse_env_plugins(plugins) == 1){
            LOG(LOG_ERROR, "Failed to parse plugins from environment variable");
            LOG_EXCEPTION_STACK();
        }
    }

    if(has_exception())
        return 1;

    return 0;
}

int apply_program_arguments(ProxyProgramArguments args){
    if(args.log_path != NULL){
        set_variable(
                (ConfigVariable){
                        .name = "logs",
                        .description = "Path to the log file",
                        .data.string = alloc_string_array(1, NULL, copy_string(args.log_path, NULL)),
                        .type = STRING,
                        .count = 1
                }
        );
    }

    if(has_exception())
        return 1;
    return 0;
}

int setup_default_variables(char* current_dir){
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return 1;
    }

    set_variable(
            (ConfigVariable){
                    .name =  copy_string("logs", NULL),
                    .description = copy_string("Path to the log file", NULL),
                    .data.string = alloc_string_array(1, NULL, copy_string("proxy.log", NULL)),
                    .type = STRING,
                    .count = 1
            }
    );

    char* plugins_dir = calloc(strlen(current_dir) + strlen("/plugins/") + 1, 1);
    if(plugins_dir == NULL){
        PUSH_EXCEPTION("Failed to allocate memory for plugins dir");
        return 1;
    }
    strcpy(plugins_dir, current_dir);
    strcat(plugins_dir, "/plugins/");


    set_variable(
            (ConfigVariable){
                    .name = copy_string("plugins_dir", NULL),
                    .description = copy_string("Plugins Dir", NULL),
                    .data.string = alloc_string_array(1, NULL, plugins_dir),
                    .type = STRING,
                    .count = 1
            }
            );

    if(has_exception())
        return 1;

    return 0;
}

int init_config(ProxyProgramArguments args){
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return 1;
    }


    if(setup_default_variables(args.proxy_path) == 1){
        LOG(LOG_ERROR, "Failed to setup default variables");
        LOG_EXCEPTION_STACK();
    }


    //Determine config path
    char* env_config_path = getenv("PROXY_CONFIG_PATH");
    if(env_config_path != NULL){
        master_config_path = env_config_path;
    }
    char* args_config_path = args.config_path;
    if(args_config_path != NULL){
        master_config_path = args_config_path;
    }


    LOG(LOG_INFO, "Begin parsing config %s", master_config_path);
    if(parse_config(master_config_path) == 1){
        LOG(LOG_ERROR, "Error while parsing config file");
        LOG_EXCEPTION_STACK();
    }


    if(apply_environment() == 1){
        LOG(LOG_ERROR, "Error while applying env");
        LOG_EXCEPTION_STACK();
    }


    if(apply_program_arguments(args) == 1){
        LOG(LOG_ERROR, "Error while applying program args");
        LOG_EXCEPTION_STACK();
    }



    return 0;
}


int define_variable(ConfigVariable variable){
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return 1;
    }

    struct HashableConfigVariable* var = malloc(sizeof(struct HashableConfigVariable));
    if(var == NULL){
        PUSH_EXCEPTION("Failed to allocate memory for variable");
        return 1;
    }

    var->var = variable;
    HASH_ADD_KEYPTR(hh, vars_hash_table, var->var.name, strlen(var->var.name), var);
    vars_count++;

    return 0;
}

ConfigVariable get_variable(const char* name) {
    if (!was_created) {
        PUSH_EXCEPTION("Config table was not created");
        return BAD_VARIABLE;
    }

    struct HashableConfigVariable *var;
    HASH_FIND(hh, vars_hash_table, name, strlen(name), var);
    if (var == NULL) {
        LOG(LOG_WARNING, "Variable %s not found", name);
        return BAD_VARIABLE;
    }
    return var->var;
}

int set_variable(ConfigVariable variable){
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return 1;
    }

    struct HashableConfigVariable* var;
    HASH_FIND(hh, vars_hash_table, variable.name, strlen(variable.name), var);
    if(var == NULL){
        return define_variable(variable);
    }

    free_variable(&var->var); // TODO: if we set_variable, the old one should be freed?
    var->var = variable;
    return 0;
}

bool does_variable_exists(const char* name){
    if(!was_created){
        PUSH_EXCEPTION("Config table was not created");
        return false;
    }

    struct HashableConfigVariable* var;
    HASH_FIND(hh, vars_hash_table, name, strlen(name), var);
    if(var == NULL){
        return false;
    }
    return true;
}


// CLEANING


void free_variable(ConfigVariable* var){
    if(var->type == UNDEFINED){
        return;
    }

    free(var->name);
    var->name = NULL;
    free(var->description);
    var->description = NULL;

    //printf("FREEING %lld\n", (long long )var->data.data);

    switch (var->type) {
        case INTEGER:
            free(var->data.integer);
            var->data.integer = NULL;
            break;
        case REAL:
            free(var->data.real);
            var->data.real = NULL;
            break;
        case STRING:
            for(int i = 0; i < var->count; i++){
                free(var->data.string[i]);
            }
            free(var->data.string);
            var->data.string = NULL;
            break;
        case UNDEFINED:
            break;
    }
}

void free_vars(void){
    if(!was_created){
        return;
    }

    struct HashableConfigVariable *var, *tmp;
    HASH_ITER(hh, vars_hash_table, var, tmp) {
        HASH_DEL(vars_hash_table, var);
        free_variable(&var->var);
        //if(var != NULL) // Otherwise Clion will show warning
        free(var);
    }


//    for(int i = 0; i < vars_count; i++){
//        free_variable(&vars[i]);
//    }
//    free(vars);
//    vars = NULL;
//    vars_count = 0;
//    vars_capacity = 0;
}

int destroy_config_table(void){
    if(!was_created){
        return 1;
    }

//    free(vars);
//    vars = NULL;
//    vars_count = 0;
//    vars_capacity = 0;
    free_vars();

    was_created = false;
    return 0;
}
