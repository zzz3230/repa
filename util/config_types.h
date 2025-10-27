#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H
#include "uthash/uthash.h"


typedef struct proxy_program_arguments {
    char* config_path;
    char* log_path;
    char* proxy_path;
} ProxyProgramArguments;

typedef union ConfigData{
    void* data;
    int64_t* integer;
    double* real;
    char** string;
} ConfigData;

typedef enum ConfigVarType{
    UNDEFINED = 0,
    INTEGER = 1,
    REAL,
    STRING
} ConfigVarType;

typedef struct ConfigVariable{
    char* name;
    char* description;
    ConfigData  data;
    ConfigVarType type;
    int count;
} ConfigVariable;

struct HashableConfigVariable{
    ConfigVariable var;
    UT_hash_handle hh;
};

#endif //CONFIG_TYPES_H
