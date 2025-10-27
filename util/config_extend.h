//
// Created by zzz32 on 10/24/2025.
//

#ifndef REPA_SERVER_CONFIG_EXTEND_H
#define REPA_SERVER_CONFIG_EXTEND_H

int get_int_config_var_or_default(char* name, int def, bool log_warn);
char* get_string_config_var_or_default(char* name, char* def, bool log_warn);
int64_t get_int64_config_var_or_default(char* name, int64_t def, bool log_warn);
#endif //REPA_SERVER_CONFIG_EXTEND_H
