#ifndef CONEXIONES_H_
#define CONEXIONES_H_

#include <commons/collections/list.h>
#include <commons/config.h>
#include <commons/log.h>
#include <commons/string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "./logger.h"

// PROTOTIPOS DE TUS FUNCIONES (que necesitan t_log de arriba)
int create_connection(t_log *logger, char *port, char *ip);
void destroy_connection(t_log *logger, int *socket_fd);
int wait_custommer(t_log *logger, int socket_servidor);
int listen_server(t_log *logger, int connection, char *module);
int start_server(t_log *logger, char *ip, char *puerto);
bool is_connection_active(t_log *logger, int socket_fd);
void listen_to_master(t_log* logger, int socket_fd, char** out_final_reason);


#endif /* CONEXIONES_H_ */