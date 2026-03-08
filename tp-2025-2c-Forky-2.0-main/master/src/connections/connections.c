
#include "./connections.h"
#include <utils/protocol.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>

pthread_mutex_t clientes_mutex = PTHREAD_MUTEX_INITIALIZER;

int create_server(t_log *logger, t_config_master *config) {
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

t_client_connection* accept_clients(t_log *logger, int server_socket, t_list *clientes, int *next_id)
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
    cliente->type = CLIENT_TYPE_UNKNOWN;
    cliente->worker_id = 0;

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

void* check_connections_thread(void* arg) {
    check_conn_args_t* args = (check_conn_args_t*)arg;
    t_log* logger = args->logger;
    t_list* clientes = args->clientes;
    int* worker_count = args->worker_count;
    pthread_mutex_t* worker_count_mutex = args->worker_count_mutex;
    t_list* workers_list = args->workers_list;
    t_dictionary* workers_by_socket = args->workers_by_socket;
    pthread_rwlock_t* workers_list_rwlock = args->workers_list_rwlock;
    t_list* query_sessions = args->query_sessions;
    pthread_rwlock_t* query_sessions_rwlock = args->query_sessions_rwlock;

    while (true) {
        usleep(2000000); // 2 segundos - dar más tiempo antes de verificar

        pthread_mutex_lock(&clientes_mutex);
        for (int i = 0; i < list_size(clientes); i++) {
            t_client_connection* cliente = list_get(clientes, i);
            
            // Para Workers, ser más tolerante: verificar múltiples veces antes de considerar desconectado
            bool really_disconnected = false;
            if (cliente->type == CLIENT_TYPE_WORKER) {
                // CRÍTICO: NO verificar Workers IDLE - están esperando asignación legítimamente
                // Solo verificar Workers que están ocupados ejecutando queries
                bool worker_is_busy = false;
                if (workers_by_socket && workers_list_rwlock) {
                    pthread_rwlock_rdlock(workers_list_rwlock);
                    char key[16];
                    snprintf(key, sizeof(key), "%d", cliente->socket_conn);
                    t_worker_info *winfo = (t_worker_info *)dictionary_get(workers_by_socket, key);
                    if (winfo) {
                        worker_is_busy = winfo->is_busy;
                    }
                    pthread_rwlock_unlock(workers_list_rwlock);
                }
                
                // Solo verificar conexión si el Worker está ocupado
                if (worker_is_busy) {
                    // Verificar 3 veces con pequeño intervalo
                    int disconnected_count = 0;
                    for (int check = 0; check < 3; check++) {
                        if (!is_connection_active(logger, cliente->socket_conn)) {
                            disconnected_count++;
                        }
                        if (check < 2) usleep(100000); // 100ms entre checks
                    }
                    really_disconnected = (disconnected_count >= 2); // 2 de 3 checks fallaron
                } else {
                    // Worker IDLE - no verificar, confiar en que está disponible
                    really_disconnected = false;
                }
            } else {
                // Para Query Controls, una sola verificación es suficiente
                really_disconnected = !is_connection_active(logger, cliente->socket_conn);
            }
            
            if (really_disconnected) {
                // Si era un Worker, decrementar el contador y remover de workers_list
                if (cliente->type == CLIENT_TYPE_WORKER && worker_count && worker_count_mutex) {
                    // Buscar si este Worker tenía una query activa
                    t_query_session* affected_query = NULL;
                    if (query_sessions && query_sessions_rwlock) {
                        pthread_rwlock_rdlock(query_sessions_rwlock);
                        for (int j = 0; j < list_size(query_sessions); j++) {
                            t_query_session* sess = list_get(query_sessions, j);
                            if (sess && sess->worker_socket == cliente->socket_conn && sess->is_active) {
                                affected_query = sess;
                                break;
                            }
                        }
                        pthread_rwlock_unlock(query_sessions_rwlock);
                    }
                    
                    pthread_mutex_lock(worker_count_mutex);
                    if (*worker_count > 0) {
                        (*worker_count)--;
                    }
                    int current_worker_count = *worker_count;
                    pthread_mutex_unlock(worker_count_mutex);
                    
                    // Remover de workers_list y workers_by_socket
                    if (workers_list && workers_list_rwlock && workers_by_socket) {
                        pthread_rwlock_wrlock(workers_list_rwlock);
                        for (int j = 0; j < list_size(workers_list); j++) {
                            t_worker_info* winfo = list_get(workers_list, j);
                            if (winfo && winfo->socket_conn == cliente->socket_conn) {
                                list_remove(workers_list, j);
                                
                                // Remover del hash map
                                char key[16];
                                snprintf(key, sizeof(key), "%d", cliente->socket_conn);
                                dictionary_remove(workers_by_socket, key);
                                
                                free_worker_info(winfo);
                                break;
                            }
                        }
                        pthread_rwlock_unlock(workers_list_rwlock);
                    }
                    
                    // Log de desconexión de Worker
                    t_metadata* m = metadata_create(LOG_CORE);

                    if (affected_query) {
                        logger_info(
                            logger,
                            string_from_format(
                                "## Se desconecta el Worker %u - Se finaliza la Query %u - Cantidad total de Workers: %d",
                                cliente->worker_id,
                                affected_query->query_id,
                                current_worker_count
                            ),
                            m
                        );
                         protocol_send_query_result(affected_query->querycontrol_socket, affected_query->query_id, "ERROR");
                                pthread_rwlock_wrlock(query_sessions_rwlock);
                                for (int j = 0; j < list_size(query_sessions); j++) {
                                        t_query_session *sess = list_get(query_sessions, j);
                                        if (sess == affected_query) {
                                            list_remove(query_sessions, j);
                                            free_query_session(sess);
                                            break;
                                        }
                                    }
                                pthread_rwlock_unlock(query_sessions_rwlock);
                    } else {
                        logger_info(
                            logger,
                            string_from_format(
                                "## Se desconecta el Worker %u - Cantidad total de Workers: %d",
                                cliente->worker_id,
                                current_worker_count
                            ),
                            m
                        );
                    }
                    metadata_clear(m);
            
            }    
                           
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

void launch_check_connections_thread(t_log* logger, t_list* clientes, int* worker_count, pthread_mutex_t* worker_count_mutex, t_list* workers_list, t_dictionary* workers_by_socket, pthread_rwlock_t* workers_list_rwlock, t_list* query_sessions, pthread_rwlock_t* query_sessions_rwlock) {
    check_conn_args_t* args = malloc(sizeof(check_conn_args_t));
    args->logger = logger;
    args->clientes = clientes;
    args->worker_count = worker_count;
    args->worker_count_mutex = worker_count_mutex;
    args->workers_list = workers_list;
    args->workers_by_socket = workers_by_socket;
    args->workers_list_rwlock = workers_list_rwlock;
    args->query_sessions = query_sessions;
    args->query_sessions_rwlock = query_sessions_rwlock;

    pthread_t thread;
    pthread_create(&thread, NULL, check_connections_thread, args);
    pthread_detach(thread);
}

int handle_client_connection(t_log *logger, int client_socket) {
    if (!logger || client_socket < 0) {
        return -1;
    }
    
    t_metadata *meta = metadata_create(LOG_SOCKET);
    metadata_add(meta, "socket", string_from_format("%d", client_socket));
    logger_info(logger, "Esperando identificación de cliente.", meta);
    metadata_clear(meta);

    // Nota: la identificación y procesamiento inicial del primer mensaje (submit)
    // se realiza ahora en el hilo cliente (`handle_client_thread` en main.c).
    // Esta función se mantiene como punto de logging/identificación ligero.
    return 0;
}

void process_query_control_message(t_log *logger, int client_socket) {
    if (!logger || client_socket < 0) return;

    t_metadata *meta = metadata_create(LOG_SOCKET);
    metadata_add(meta, "socket", string_from_format("%d", client_socket));
    metadata_add(meta, "type", "QueryControl");
    logger_info(logger, "Procesando mensaje de QueryControl.", meta);
    metadata_clear(meta);

    // TODO: Implementar protocolo específico para QueryControl
    // - Recibir queries con prioridad usando protocol_recv_query_submit()
    // - Crear t_query_session para asociarw query con QueryControl
    // - Enviar confirmaciones usando protocol_send_query_confirm()
    // - Mantener conexión activa hasta que query termine
    // - Enviar resultados usando protocol_send_query_result()
}

void process_worker_message(t_log *logger, int client_socket) {
    if (!logger || client_socket < 0) return;

    t_metadata *meta = metadata_create(LOG_SOCKET);
    metadata_add(meta, "socket", string_from_format("%d", client_socket));
    metadata_add(meta, "type", "Worker");
    logger_info(logger, "Procesando mensaje de Worker.", meta);
    metadata_clear(meta);

    // TODO: Implementar protocolo específico para Worker
    // - Recibir registro usando protocol_recv_worker_register()
    // - Enviar confirmación usando protocol_send_worker_register_ack()
    // - Asignar queries usando protocol_send_query_assign()
    // - Recibir resultados usando protocol_recv_query_complete()
    // - REENVIAR resultados al QueryControl correspondiente
}
