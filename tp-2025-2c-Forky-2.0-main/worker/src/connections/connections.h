#ifndef WORKER_CONNECTIONS_H
#define WORKER_CONNECTIONS_H

#include <commons/log.h> 
#include <utils/sockets.h>

typedef struct t_config_worker t_config_worker;

// Conexiones con Master
int connect_master_server(t_log *logger, t_config_worker *config, int worker_id);
void disconnect_master_server(t_log *logger, int master_conn);
int is_master_server_active(t_log *logger, int master_conn);

// Conexiones con Storage (básicas)
int connect_storage_server(t_log *logger, t_config_worker *config);
void disconnect_storage_server(t_log *logger, int storage_conn);
int is_storage_server_active(t_log *logger, int storage_conn);

#endif
