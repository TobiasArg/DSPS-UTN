#ifndef QUERY_CONTROL_CONNECTIONS_H
#define QUERY_CONTROL_CONNECTIONS_H

#include <utils/sockets.h>
#include "../main.h"
#include "utils/protocol.h" 

int connect_master_server(t_log *logger, t_config_query_control *config);
void disconnect_master_server(t_log *logger, int master_conn);
//bool is_master_server_active(t_log *logger, int master_conn);
void listen_to_master(t_log* logger, int socket_fd, char** out_final_reason);

#endif /* QUERY_CONTROL_CONNECTIONS_H */