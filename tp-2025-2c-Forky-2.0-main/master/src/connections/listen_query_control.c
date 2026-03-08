#include "./connections.h"
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

int create_query_controls_server(t_log *logger, t_config_master *config) {
    if (!logger || !config) return -1;

    char *puerto_str = string_from_format("%d", config->puerto_master);
    int server_socket = start_server(logger, config->ip_master, puerto_str);
    free(puerto_str);

    if (server_socket < 0) {
        logger_error(logger, "No se pudo crear el servidor de query_controls.", metadata_create(LOG_SOCKET));
        return -1;
    }

    return server_socket;
}

t_client_connection* accept_query_controls_client(t_log *logger, int server_socket, t_list *clientes, int *next_id)
{
    if (!logger || !clientes || !next_id || server_socket < 0) return NULL;

    struct sockaddr_in dir_cliente;
    socklen_t tam_dir = sizeof(dir_cliente);
    int client_fd = accept(server_socket, (struct sockaddr*)&dir_cliente, &tam_dir);
    if (client_fd < 0) {
        return NULL;
    }

    t_client_connection *cliente = malloc(sizeof(t_client_connection));
    cliente->client_id = (*next_id)++;
    cliente->socket_conn = client_fd;

    pthread_mutex_lock(&clientes_mutex);
    list_add(clientes, cliente);
    pthread_mutex_unlock(&clientes_mutex);

    t_metadata *meta = metadata_create(LOG_SOCKET);
    metadata_add(meta, "id", string_from_format("%d", cliente->client_id));
    metadata_add(meta, "socket", string_from_format("%d", cliente->socket_conn));
    // logger_info(logger, "Nuevo cliente fue \x1b[32mConectado exitosamente!\x1b[0m.", meta);
    metadata_clear(meta);

    return cliente;
}

void* check_queries_connections_thread(void* arg) {
    check_conn_args_t* args = (check_conn_args_t*)arg;
    t_log* logger = args->logger;
    t_list* clientes = args->clientes;

    while (true) {
        usleep(500000); // 0.5s

        pthread_mutex_lock(&clientes_mutex);
        for (int i = 0; i < list_size(clientes); i++) {
            t_client_connection* cliente = list_get(clientes, i);
            if (!is_connection_active(logger, cliente->socket_conn)) {
                close(cliente->socket_conn);
                list_remove(clientes, i);
                free(cliente);
                i--;
            }
        }
        pthread_mutex_unlock(&clientes_mutex);
    }

    free(arg);
    return NULL;
}

void launch_check_queries_connections_thread(t_log* logger, t_list* clientes) {
    check_conn_args_t* args = malloc(sizeof(check_conn_args_t));
    args->logger = logger;
    args->clientes = clientes;

    pthread_t thread;
    pthread_create(&thread, NULL, check_queries_connections_thread, args);
    pthread_detach(thread);
}
