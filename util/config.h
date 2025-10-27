#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>
#include "stdbool.h"
#include "config_types.h"


bool is_debug_mode(void);
void set_debug_mode(bool mode);

int create_config_table(void);
int destroy_config_table(void);

int parse_config(const char* path);
int define_variable(ConfigVariable variable);
ConfigVariable  get_variable(const char* name);
int set_variable(ConfigVariable variable);
bool does_variable_exists(const char* name);
int init_config(ProxyProgramArguments args);
void free_variable(ConfigVariable* var);
#endif //CONFIG_H
