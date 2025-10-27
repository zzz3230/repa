//
// Created by zzz32 on 10/23/2025.
//

#ifndef REPA_SERVER_STATS_SERVICE_H
#define REPA_SERVER_STATS_SERVICE_H

/*

 repa.stats.total_commands_processed
 repa.stats.cmd_get
 repa.stats.cmd_set
 repa.stats.cmd_del
 repa.stats.used_memory_bytes
 repa.stats.current_connections

*/

#define STATS_TOTAL_COMMANDS    "repa.stats.total_commands_processed"
#define STATS_CMD_GET           "repa.stats.cmd_get"
#define STATS_CMD_SET           "repa.stats.cmd_set"
#define STATS_CMD_DEL           "repa.stats.cmd_del"
#define STATS_CMD_USED_MEM      "repa.stats.used_memory_bytes"
#define STATS_CMD_CUR_CONN      "repa.stats.current_connections"

void init_stats_service();
char** get_available_stats();
int stats_add(char* name, int value);
void stop_stats_service();

#endif //REPA_SERVER_STATS_SERVICE_H
