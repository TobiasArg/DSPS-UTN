
#ifndef MASTER_CONNECTIONS_H
#define MASTER_CONNECTIONS_H

#include <utils/sockets.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/collections/list.h>
#include "../main.h"

typedef struct {
    int client_id;
    int socket_conn;
    client_type_t type;
} t_client_connection_extended;

typedef struct {
    t_log* logger;
    t_list* clientes;
    int* worker_count;
    pthread_mutex_t* worker_count_mutex;
    t_list* workers_list;
    t_dictionary* workers_by_socket;       // Hash map para eliminación O(1)
    pthread_rwlock_t* workers_list_rwlock; // Cambiado a rwlock
    t_list* query_sessions;
    pthread_rwlock_t* query_sessions_rwlock; // Cambiado a rwlock
} check_conn_args_t;

extern pthread_mutex_t clientes_mutex;

int create_server(t_log *logger, t_config_master *config);
t_client_connection* accept_clients(t_log *logger, int server_socket, t_list *clientes, int *next_id);
void launch_check_connections_thread(t_log* logger, t_list* clientes, int* worker_count, pthread_mutex_t* worker_count_mutex, t_list* workers_list, t_dictionary* workers_by_socket, pthread_rwlock_t* workers_list_rwlock, t_list* query_sessions, pthread_rwlock_t* query_sessions_rwlock);

// Nuevas funciones para manejo de diferentes tipos de clientes
int handle_client_connection(t_log *logger, int client_socket);
void process_query_control_message(t_log *logger, int client_socket);
void process_worker_message(t_log *logger, int client_socket);

#endif /* MASTER_CONNECTIONS_H */
