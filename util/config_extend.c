

#include "config.h"
#include "../logger.h"

int get_int_config_var_or_default(char* name, int def, bool log_warn){
    ConfigVariable portVar = get_variable(name);
    int value = def;
    if(portVar.type == INTEGER){
        value = (int)*portVar.data.integer;
    }
    else if(log_warn){
        LOG(LOG_WARNING, "Config var '%s' undefined, using default value = %d", name, def);
    }

    return value;
}

int64_t get_int64_config_var_or_default(char* name, int64_t def, bool log_warn){
    ConfigVariable portVar = get_variable(name);
    int64_t value = def;
    if(portVar.type == INTEGER){
        value = (int64_t)*portVar.data.integer;
    }
    else if(log_warn){
        LOG(LOG_WARNING, "Config var '%s' undefined, using default value = %lld", name, def);
    }

    return value;
}

char* get_string_config_var_or_default(char* name, char* def, bool log_warn){
    ConfigVariable portVar = get_variable(name);
    char* value = def;
    if(portVar.type == STRING){
        value = portVar.data.string[0];
    }
    else if(log_warn){
        if(def != NULL)
            LOG(LOG_WARNING, "Config var '%s' undefined, using default value = %s", name, def);
        else
            LOG(LOG_WARNING, "Config var '%s' undefined, using default value = NULL", name);
    }

    return value;
}