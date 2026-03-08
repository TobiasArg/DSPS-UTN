#include "./main.h"
#include "./connections/connections.h"
#include <utils/protocol.h>

#define MODULE_NAME "Master"
#define CONFIG_PATH "./master.config"
#define TOTAL_ARGS 1 // allow at most 1 parameter: [archivo_config]
#define MAX_QUERIES 100

int main(int argc, char *argv[])
{
    // Validar que se pase como máximo 1 parámetro opcional: [archivo_config]
    if (!valid_max_params(argc, TOTAL_ARGS))
    {
        error_show("[[Abortar!]] No se respeto el formato: ./bin/master [archivo_config]");
        return EXIT_FAILURE;
    }

    // Determinar ruta de configuración (por defecto o pasada por argv)
    char *config_path = (argc > 1) ? argv[1] : CONFIG_PATH;

    // Cargar configuración
    t_config_master *config = cargar_configuracion(config_path);
    if (!config)
    {
        error_show("[[Abortar!]]La configuración fue NULA.");
        return EXIT_FAILURE;
    }

    // Inicializar logger
    t_log *logger = logger_create("master.log", "master", log_level_from_string(config->log_level));
    log_debug(logger, "[✓] Módulo inicializado Ok. Cfg=%s", config_path);

    // Validar algoritmo de planificación
    if (strcmp(config->algoritmo_planning, "FIFO") != 0 && 
        strcmp(config->algoritmo_planning, "PRIORIDADES") != 0)
    {
        log_error(logger, "[x] Algoritmo de planificación inválido: %s. Debe ser FIFO o PRIORIDADES.", config->algoritmo_planning);
        return EXIT_FAILURE;
    }

    // Crear servidor de query controls
    int server_socket = create_server(logger, config);
    if (server_socket < 0)
    {
        log_error(logger, "[x] No se pudo iniciar el servidor de query_controls.");
        return EXIT_FAILURE;
    }

    // Lista de clientes
    t_list *clientes = list_create();

    // Lista de query sessions y rwlock para protegerla (permite lecturas concurrentes)
    t_list *query_sessions = list_create();
    pthread_rwlock_t query_sessions_rwlock = PTHREAD_RWLOCK_INITIALIZER;
    
    // Hash map para búsquedas O(1) de queries por ID (protegido por query_sessions_rwlock)
    t_dictionary *queries_by_id = dictionary_create();
    
    // Condition variable para scheduler reactivo (señalización de eventos)
    pthread_cond_t scheduler_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Contador para client IDs (usado por accept_clients)
    int client_next_id = 0;

    // Contador autoincremental para IDs de query y su mutex
    uint32_t query_next_id = 0;
    pthread_mutex_t query_next_id_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Contador de workers activos (para nivel de multiprocesamiento)
    int worker_count = 0;
    pthread_mutex_t worker_count_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Lista de workers (disponibles y ocupados) y rwlock para protegerla
    t_list *workers_list = list_create();
    pthread_rwlock_t workers_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;
    
    // Hash map para búsquedas O(1) de workers por socket (protegido por workers_list_rwlock)
    t_dictionary *workers_by_socket = dictionary_create();

    launch_check_connections_thread(logger, clientes, &worker_count, &worker_count_mutex, workers_list, workers_by_socket, &workers_list_rwlock, query_sessions, &query_sessions_rwlock);

    // Preparar argumentos del scheduler
    pthread_t scheduler_tid;
    t_scheduler_args *scheduler_args = malloc(sizeof(t_scheduler_args));
    if (!scheduler_args) {
        log_error(logger, "[x] Error de memoria al crear scheduler_args");
        return EXIT_FAILURE;
    }
    scheduler_args->logger = logger;
    scheduler_args->query_sessions = query_sessions;
    scheduler_args->queries_by_id = queries_by_id;
    scheduler_args->query_sessions_rwlock = &query_sessions_rwlock;
    scheduler_args->workers_list = workers_list;
    scheduler_args->workers_by_socket = workers_by_socket;
    scheduler_args->workers_list_rwlock = &workers_list_rwlock;
    scheduler_args->scheduler_cond = &scheduler_cond;
    scheduler_args->scheduler_mutex = &scheduler_mutex;
    scheduler_args->tiempo_aging_ms = config->tiempo_aging;

    // Lanzar el scheduler según ALGORITMO_PLANIFICACION
    if (strcmp(config->algoritmo_planning, "FIFO") == 0)
    {
        pthread_create(&scheduler_tid, NULL, scheduler_thread, scheduler_args);
        pthread_detach(scheduler_tid);

        log_info(logger, "[✓] Ejecutando las instrucciones con el planificador FIFO");
    }
    else if (strcmp(config->algoritmo_planning, "PRIORIDADES") == 0)
    {
        pthread_create(&scheduler_tid, NULL, scheduler_priority_thread, scheduler_args);
        pthread_detach(scheduler_tid);
        
        // Lanzar aging_thread solo si TIEMPO_AGING > 0
        if (config->tiempo_aging > 0) {
            pthread_t aging_tid;
            t_scheduler_args *aging_args = malloc(sizeof(t_scheduler_args));
            if (!aging_args) {
                log_error(logger, "[x] Error de memoria al crear aging_args");
                return EXIT_FAILURE;
            }
            // Copiar los mismos argumentos
            aging_args->logger = logger;
            aging_args->query_sessions = query_sessions;
            aging_args->queries_by_id = queries_by_id;
            aging_args->query_sessions_rwlock = &query_sessions_rwlock;
            aging_args->workers_list = workers_list;
            aging_args->workers_by_socket = workers_by_socket;
            aging_args->workers_list_rwlock = &workers_list_rwlock;
            aging_args->scheduler_cond = &scheduler_cond;
            aging_args->scheduler_mutex = &scheduler_mutex;
            aging_args->tiempo_aging_ms = config->tiempo_aging;
            
            pthread_create(&aging_tid, NULL, aging_thread, aging_args);
            pthread_detach(aging_tid);
            
            log_info(logger, "[✓] Ejecutando las instrucciones con el planificador por PRIORIDADES (con Aging)");
        } else {
            log_info(logger, "[✓] Ejecutando las instrucciones con el planificador por PRIORIDADES (sin Aging)");
        }
    }
    
    // Lanzar watchdog thread (para todos los algoritmos)
    pthread_t watchdog_tid;
    t_watchdog_args *watchdog_args = malloc(sizeof(t_watchdog_args));
    if (!watchdog_args) {
        log_error(logger, "[x] Error de memoria al crear watchdog_args");
        return EXIT_FAILURE;
    }
    watchdog_args->logger = logger;
    watchdog_args->query_sessions = query_sessions;
    watchdog_args->query_sessions_rwlock = &query_sessions_rwlock;
    watchdog_args->workers_list = workers_list;
    watchdog_args->workers_by_socket = workers_by_socket;
    watchdog_args->workers_list_rwlock = &workers_list_rwlock;
    watchdog_args->check_interval_sec = 10;  // Revisar cada 10 segundos
    watchdog_args->query_timeout_sec = 120;  // Timeout de 120 segundos (2 minutos)
    
    pthread_create(&watchdog_tid, NULL, watchdog_thread, watchdog_args);
    pthread_detach(watchdog_tid);
    
    log_info(logger, "[✓] Watchdog thread iniciado (interval=10s, timeout=120s)");

    while (true)
    {
        t_client_connection *nuevo_cliente = accept_clients(logger, server_socket, clientes, &client_next_id);
        if (nuevo_cliente)
        {
            // Procesar el cliente en un hilo separado para mantener conexión persistente
            pthread_t client_thread;
            t_client_handler_args *args = malloc(sizeof(t_client_handler_args));
            if (!args) {
                log_error(logger, "[x] Error de memoria al crear client_handler_args");
                // El cliente quedará conectado pero sin thread handler
                // En producción podríamos cerrar la conexión aquí
                continue;
            }
            args->logger = logger;
            args->client = nuevo_cliente;
            args->query_sessions = query_sessions;
            args->queries_by_id = queries_by_id;
            args->next_query_id = &query_next_id;
            args->query_sessions_rwlock = &query_sessions_rwlock;
            args->next_id_mutex = &query_next_id_mutex;
            args->worker_count = &worker_count;
            args->worker_count_mutex = &worker_count_mutex;
            args->workers_list = workers_list;
            args->workers_by_socket = workers_by_socket;
            args->workers_list_rwlock = &workers_list_rwlock;
            args->scheduler_cond = &scheduler_cond;
            args->scheduler_mutex = &scheduler_mutex;

            pthread_create(&client_thread, NULL, handle_client_thread, args);
            pthread_detach(client_thread);
        }
        usleep(100000); // 100ms - Evita busy-wait si accept falla
    }

    return EXIT_SUCCESS;
}

void *handle_client_thread(void *arg)
{
    t_client_handler_args *args = (t_client_handler_args *)arg;

    if (!args)
    {
        return NULL;
    }

    t_log *logger = args->logger;
    t_client_connection *client = args->client;

    if (!logger || !client)
    {
        free(args);
        return NULL;
    }

    log_trace(logger, "[✓] Iniciando hilo para cliente persistente: %d", client->client_id);

    // Peek the first byte (opcode) to decide if this is a QueryControl submit or Worker register
    uint8_t op = 0;
    int peeked = recv(client->socket_conn, &op, 1, MSG_PEEK);
    if (peeked > 0 && op == OP_QUERY_SUBMIT)
    {
        // It's a QueryControl submit
        char *query_path = NULL;
        uint32_t priority = 0;
        int recv_res = protocol_recv_query_submit(client->socket_conn, &query_path, &priority);
        if (recv_res == 0 && query_path)
        {
            // Asignar ID al Query respetando el mutex
            uint32_t assigned_id = 0;
            if (args->next_id_mutex && args->next_query_id)
            {
                pthread_mutex_lock(args->next_id_mutex);
                assigned_id = *args->next_query_id;
                (*args->next_query_id)++;
                pthread_mutex_unlock(args->next_id_mutex);
            }

            // Crear el t_query_session
            t_query_session *session = malloc(sizeof(t_query_session));
            if (!session) {
                log_error(logger, "[x] Error de memoria al crear query_session para QueryID %u", assigned_id);
                free(query_path);
                free(args);
                return NULL;
            }
            
            session->query_id = assigned_id;
            session->querycontrol_socket = client->socket_conn;
            session->worker_socket = -1;
            session->query_path = string_duplicate(query_path);
            if (!session->query_path) {
                log_error(logger, "[x] Error de memoria al duplicar query_path para QueryID %u", assigned_id);
                free(session);
                free(query_path);
                free(args);
                return NULL;
            }
            session->priority = priority;
            session->original_priority = priority;  // Guardar prioridad inicial para aging
            session->is_active = false;
            session->pc = 0;                        // PC inicial en 0 (nueva query)
            atomic_store(&session->ready_since, time(NULL)); // Timestamp atómico

            // Agregar a query_sessions protegido por rwlock
            if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
            {
                pthread_rwlock_wrlock(args->query_sessions_rwlock);
                list_add(args->query_sessions, session);
                
                // Agregar al hash map para búsqueda O(1)
                char key[16];
                snprintf(key, sizeof(key), "%u", assigned_id);
                dictionary_put(args->queries_by_id, key, session);
                
                pthread_rwlock_unlock(args->query_sessions_rwlock);
                
                // Señalizar scheduler: nueva query disponible
                if (args->scheduler_mutex && args->scheduler_cond) {
                    pthread_mutex_lock(args->scheduler_mutex);
                    pthread_cond_signal(args->scheduler_cond);
                    pthread_mutex_unlock(args->scheduler_mutex);
                }
            }

            // Enviar confirmación al QueryControl
            if (protocol_send_query_confirm(client->socket_conn, assigned_id) < 0)
            {
                log_error(logger, "[x] Fallo al enviar confirmación de QueryID %u al Query Control", assigned_id);
                // Cleanup: remover session
                if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
                {
                    pthread_rwlock_wrlock(args->query_sessions_rwlock);
                    // Buscar el índice del elemento y removerlo por índice
                    for (int _i = 0; _i < list_size(args->query_sessions); _i++)
                    {
                        void *_elem = list_get(args->query_sessions, _i);
                        if (_elem == (void *)session)
                        {
                            list_remove(args->query_sessions, _i);
                            break;
                        }
                    }
                    
                    // Remover del hash map
                    char key[16];
                    snprintf(key, sizeof(key), "%u", assigned_id);
                    dictionary_remove(args->queries_by_id, key);
                    
                    pthread_rwlock_unlock(args->query_sessions_rwlock);
                }
                free_query_session(session);
                free(query_path);
                free(args);
                return NULL;
            }

            // Logs obligatorios: Conexión de Query Control
            t_metadata *m = metadata_create(LOG_CORE);

            // Nivel multiprocesamiento: cantidad de workers activos
            int current_workers = 0;
            if (args->worker_count && args->worker_count_mutex)
            {
                pthread_mutex_lock(args->worker_count_mutex);
                current_workers = *args->worker_count;
                pthread_mutex_unlock(args->worker_count_mutex);
            }
            log_info(
                logger,
                "## Se conecta un Query Control para ejecutar la Query %s con prioridad %u - Id asignado: %u. Nivel multiprocesamiento %d",
                session->query_path,
                session->priority,
                session->query_id,
                current_workers
            );

            metadata_clear(m);
            free(query_path);

        }
    }
    else if (peeked > 0 && op == OP_MASTER_WORKER_REGISTER)
    {
        // Es un Worker registrándose
        uint32_t worker_id = 0;
        if (protocol_recv_worker_register(client->socket_conn, &worker_id) == 0)
        {
            // Marcar este cliente como Worker
            client->type = CLIENT_TYPE_WORKER;
            client->worker_id = worker_id;

            // Incrementar contador de workers de forma segura
            int workers_after_increment = 0;
            if (args->worker_count && args->worker_count_mutex)
            {
                pthread_mutex_lock(args->worker_count_mutex);
                (*args->worker_count)++;
                workers_after_increment = *args->worker_count;
                pthread_mutex_unlock(args->worker_count_mutex);
            }

            // Enviar ACK al Worker
            if (protocol_send_worker_register_ack(client->socket_conn, worker_id) >= 0)
            {
                // Log obligatorio: Conexión de Worker
                t_metadata *m = metadata_create(LOG_CORE);
                log_info(
                    logger,
                    "\x1b[32m## Se conecta el Worker %u - Cantidad total de Workers: %d\x1b[0m",
                    worker_id,
                    workers_after_increment
                );

                // Agregar worker a lista de workers idle (disponible para asignación)
                t_worker_info *worker_info = malloc(sizeof(t_worker_info));
                if (!worker_info) {
                    t_metadata *m_err = metadata_create(LOG_CORE);
                    metadata_add(m_err, "WorkerID", string_from_format("%u", worker_id));
                    logger_error(logger, "Error de memoria al crear worker_info", m_err);
                    metadata_clear(m_err);
                } else {
                    worker_info->worker_id = worker_id;
                    worker_info->socket_conn = client->socket_conn;
                    worker_info->is_busy = false;
                    worker_info->awaiting_evict_response = false;
                    worker_info->evict_request_time = 0;

                    if (args->workers_list && args->workers_list_rwlock && args->workers_by_socket)
                    {
                        pthread_rwlock_wrlock(args->workers_list_rwlock);
                        list_add(args->workers_list, worker_info);
                        
                        // Agregar al hash map para búsqueda O(1)
                        char key[16];
                        snprintf(key, sizeof(key), "%d", client->socket_conn);
                        dictionary_put(args->workers_by_socket, key, worker_info);
                        
                        // Log diagnóstico: confirmar agregado con estado disponible
                        int total_workers = list_size(args->workers_list);
                        log_info(logger, "[WORKER_REGISTER] Worker %u agregado a workers_list (socket=%d, is_busy=%d, total=%d)",
                                 worker_id, client->socket_conn, worker_info->is_busy, total_workers);
                        
                        pthread_rwlock_unlock(args->workers_list_rwlock);
                        
                        // Señalizar scheduler: nuevo worker disponible
                        if (args->scheduler_mutex && args->scheduler_cond) {
                            pthread_mutex_lock(args->scheduler_mutex);
                            pthread_cond_signal(args->scheduler_cond);
                            pthread_mutex_unlock(args->scheduler_mutex);
                            log_info(logger, "[WORKER_REGISTER] Scheduler señalizado - Worker %u disponible", worker_id);
                        }
                    }
                }
            }
        }
    }

    // Mantener conexión activa hasta que la query termine
    while (is_connection_active(logger, client->socket_conn))
    {
        // Procesar mensajes del cliente
        // Si es Worker, puede recibir query complete
        if (client->type == CLIENT_TYPE_WORKER)
        {
            // Peek para ver si hay mensaje
            uint8_t op_peek = 0;
            int peek_res = recv(client->socket_conn, &op_peek, 1, MSG_PEEK | MSG_DONTWAIT);

            // CRÍTICO: Si peek devuelve 0 con opcode 0x00, el socket está cerrado
            if (peek_res > 0 && op_peek == 0x00)
            {
                log_info(logger, "[WORKER_MSG] Worker %u (socket %d) - Socket cerrado (EOF detectado)", 
                         client->worker_id, client->socket_conn);
                
                // CLEANUP: Liberar worker ANTES de salir del loop
                if (args->workers_by_socket && args->workers_list_rwlock)
                {
                    pthread_rwlock_wrlock(args->workers_list_rwlock);
                    char key[16];
                    snprintf(key, sizeof(key), "%d", client->socket_conn);
                    t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                    if (winfo) {
                        winfo->is_busy = false;
                        winfo->awaiting_evict_response = false; // Limpiar flag de EVICT pendiente
                        winfo->evict_request_time = 0;
                        log_info(logger, "[WORKER_MSG] Worker %u liberado (was busy, awaiting_evict cleared)", 
                                 client->worker_id);
                    }
                    pthread_rwlock_unlock(args->workers_list_rwlock);
                }
                
                break; // Salir del loop - el Worker se desconectó
            }

            // Log solo para opcodes no reconocidos (posibles errores)
            if (peek_res > 0)
            {
                if (op_peek != OP_MASTER_QUERY_COMPLETE && 
                    op_peek != OP_WORKER_READ_RESULT && 
                    op_peek != OP_WORKER_QUERY_FINISHED &&
                    op_peek != OP_WORKER_QUERY_ERROR &&
                    op_peek != OP_WORKER_EVICT_ACK &&
                    op_peek != 0x00)  // 0x00 ya se maneja arriba
                {
                    // Opcode no reconocido - posible error
                    log_warning(logger, "[MASTER] Opcode no reconocido del Worker %u (socket %d) - opcode=0x%02X", 
                                client->worker_id, client->socket_conn, op_peek);
                }
            }
            else if (peek_res == 0)
            {
                // Socket cerrado limpiamente (FIN recibido)
                log_info(logger, "[WORKER_MSG] Worker %u (socket %d) - Conexión cerrada (FIN)", 
                         client->worker_id, client->socket_conn);
                
                // CLEANUP: Liberar worker ANTES de salir del loop
                if (args->workers_by_socket && args->workers_list_rwlock)
                {
                    pthread_rwlock_wrlock(args->workers_list_rwlock);
                    char key[16];
                    snprintf(key, sizeof(key), "%d", client->socket_conn);
                    t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                    if (winfo) {
                        winfo->is_busy = false;
                        winfo->awaiting_evict_response = false; // Limpiar flag de EVICT pendiente
                        winfo->evict_request_time = 0;
                        log_info(logger, "[WORKER_MSG] Worker %u liberado (was busy, awaiting_evict cleared)", 
                                 client->worker_id);
                    }
                    pthread_rwlock_unlock(args->workers_list_rwlock);
                }
                
                break;
            }
            else if (peek_res < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                log_debug(logger, "[MASTER] Error en peek del Worker %u (socket %d): %s", 
                          client->worker_id, client->socket_conn, strerror(errno));
            }

            if (peek_res > 0 && op_peek == OP_MASTER_QUERY_COMPLETE)
            {
                // Worker completó una query
                uint32_t completed_query_id = 0;
                char *result = NULL;

                if (protocol_recv_query_complete(client->socket_conn, &completed_query_id, &result) == 0)
                {
                    // Buscar la session correspondiente con hash map O(1)
                    t_query_session *completed_session = NULL;

                    if (args->queries_by_id && args->query_sessions_rwlock)
                    {
                        pthread_rwlock_rdlock(args->query_sessions_rwlock);
                        
                        char key[16];
                        snprintf(key, sizeof(key), "%u", completed_query_id);
                        completed_session = (t_query_session *)dictionary_get(args->queries_by_id, key);
                        
                        pthread_rwlock_unlock(args->query_sessions_rwlock);
                    }

                    if (completed_session && result)
                    {
                        // Enviar resultado al QueryControl - CRÍTICO: verificar éxito
                        int send_result = protocol_send_query_result(completed_session->querycontrol_socket,
                                       completed_session->query_id,
                                       result);
                        
                        if (send_result != 0) {
                            log_error(logger, "[✗] Query %u: FALLO al enviar resultado al Query Control (socket %d, errno=%d: %s)", 
                                     completed_session->query_id, 
                                     completed_session->querycontrol_socket,
                                     errno, strerror(errno));
                            // NO liberar la sesión ni el worker - reintentar en próximo ciclo
                            if (result) free(result);
                            continue;
                        }
                        
                        // Log obligatorio: Finalización de Query
                        log_info(
                            logger,
                            "\x1b[33m## Se terminó la Query %u en el Worker %u\x1b[0m",
                            completed_session->query_id,
                            client->worker_id
                        );
                        log_info(logger, "\x1b[32m[✓] Query %u: Resultado enviado al Query Control\x1b[0m",
                                 completed_session->query_id);

                        // Marcar worker como disponible nuevamente con hash map O(1)
                        if (args->workers_by_socket && args->workers_list_rwlock)
                            {
                                pthread_rwlock_wrlock(args->workers_list_rwlock);
                                
                                char key[16];
                                snprintf(key, sizeof(key), "%d", client->socket_conn);
                                t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                                
                                if (winfo)
                                {
                                    winfo->is_busy = false;
                                }
                                
                                pthread_rwlock_unlock(args->workers_list_rwlock);
                            }

                            if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
                            {
                                pthread_rwlock_wrlock(args->query_sessions_rwlock);
                                for (int i = 0; i < list_size(args->query_sessions); i++)
                                {
                                    t_query_session *sess = list_get(args->query_sessions, i);
                                    if (sess == completed_session)
                                    {
                                        list_remove(args->query_sessions, i);
                                        break;
                                    }
                                }
                                
                                // Remover del hash map
                                char key[16];
                                snprintf(key, sizeof(key), "%u", completed_query_id);
                                dictionary_remove(args->queries_by_id, key);
                                
                                pthread_rwlock_unlock(args->query_sessions_rwlock);
                            }

                         free_query_session(completed_session);
                    }

                    if (result)
                        free(result);
                }
            }
            else if (peek_res > 0 && op_peek == OP_WORKER_READ_RESULT)
            {
                // Worker envía resultado de lectura (READ)
                uint32_t query_id = 0;
                char *file_tag = NULL;
                char *read_data = NULL;
                uint32_t data_size = 0;

                if (protocol_recv_worker_read_result(client->socket_conn, &query_id, &file_tag, &read_data, &data_size) == 0)
                {
                    // Buscar la session correspondiente con hash map O(1)
                    t_query_session *session = NULL;

                    if (args->queries_by_id && args->query_sessions_rwlock)
                    {
                        pthread_rwlock_rdlock(args->query_sessions_rwlock);
                        
                        char key[16];
                        snprintf(key, sizeof(key), "%u", query_id);
                        session = (t_query_session *)dictionary_get(args->queries_by_id, key);
                        
                        pthread_rwlock_unlock(args->query_sessions_rwlock);
                    }

                    if (session && read_data && file_tag)
                    {
                        // Formatear mensaje: File:Tag|Contenido
                        size_t message_len = strlen(file_tag) + 1 + data_size + 1;
                        char *formatted_message = malloc(message_len);
                        snprintf(formatted_message, message_len, "%s|%s", file_tag, read_data);
                        
                        // Reenviar mensaje de lectura al QueryControl
                        int send_result = protocol_send_query_read_message(session->querycontrol_socket,
                                                            query_id, formatted_message);
                        
                        if (send_result == 0)
                        {
                            // Log obligatorio: Envío de lectura a Query Control
                            t_metadata *m = metadata_create(LOG_CORE);
                            log_info(
                                logger,
                                "## Se envía un mensaje de lectura de la Query %u en el Worker %u al Query Control",
                                query_id,
                                client->worker_id
                            );
                        }
                        else
                        {
                            log_error(logger, "[✗] Query %u: FALLO al enviar mensaje READ al Query Control (socket %d, errno=%d: %s)",
                                     query_id, session->querycontrol_socket, errno, strerror(errno));
                            // Continuar procesando - no bloqueamos el thread por un READ que falló
                        }
                        
                        free(formatted_message);
                    }
                    else if (!session)
                    {
                        log_error(logger, "[✗] Query %u: NO SE ENCONTRÓ la sesión al recibir READ del Worker %u - READ descartado",
                                 query_id, client->worker_id);
                    }

                    if (file_tag)
                        free(file_tag);
                    if (read_data)
                        free(read_data);
                }
            }
            
            // Re-peek para procesar otro mensaje si está disponible
            peek_res = recv(client->socket_conn, &op_peek, 1, MSG_PEEK | MSG_DONTWAIT);

            // Worker notifica que finalizó exitosamente una query (END)
            if (peek_res > 0 && op_peek == OP_WORKER_QUERY_FINISHED)
            {
                uint32_t finished_query_id = 0;
                
                log_info(logger, "[MASTER] Recibido OP_WORKER_QUERY_FINISHED del Worker %u", client->worker_id);
                
                // protocol_recv_worker_query_finished() consume el opcode internamente
                if (protocol_recv_worker_query_finished(client->socket_conn, &finished_query_id) == 0)
                {
                    // Buscar la session por el socket del worker (no por query_id)
                    t_query_session *finished_session = NULL;

                    if (args->query_sessions && args->query_sessions_rwlock)
                    {
                        pthread_rwlock_rdlock(args->query_sessions_rwlock);
                        
                        for (int i = 0; i < list_size(args->query_sessions); i++)
                        {
                            t_query_session *sess = list_get(args->query_sessions, i);
                            
                            // Buscar por socket Y que esté activa
                            if (sess && sess->worker_socket == client->socket_conn && sess->is_active)
                            {
                                finished_session = sess;
                                break;
                            }
                        }
                        pthread_rwlock_unlock(args->query_sessions_rwlock);
                    }

                    if (finished_session)
                    {
                        // Enviar notificación de éxito al QueryControl - CRÍTICO: verificar éxito
                        int send_result = protocol_send_query_result(finished_session->querycontrol_socket,
                                                    finished_session->query_id,
                                                    "SUCCESS");
                        
                        if (send_result != 0) {
                            log_error(logger, "[✗] Query %u: FALLO al enviar resultado SUCCESS al Query Control (socket %d, errno=%d: %s)", 
                                     finished_session->query_id, 
                                     finished_session->querycontrol_socket,
                                     errno, strerror(errno));
                            // NO liberar la sesión ni el worker - reintentar en próximo ciclo
                            continue;
                        }
                        
                        // Log obligatorio: Finalización de Query
                        log_info(
                            logger,
                            "\x1b[33m## Se terminó la Query %u en el Worker %u\x1b[0m",
                            finished_session->query_id,
                                    client->worker_id
                        );
                        log_info(logger, "\x1b[32m[✓] Query %u: Resultado enviado al Query Control\x1b[0m",
                                 finished_session->query_id);

                        // Marcar worker como disponible nuevamente con hash map O(1)
                        if (args->workers_by_socket && args->workers_list_rwlock)
                        {
                            pthread_rwlock_wrlock(args->workers_list_rwlock);
                            
                            char key[16];
                            snprintf(key, sizeof(key), "%d", client->socket_conn);
                            t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                            
                            if (winfo)
                            {
                                winfo->is_busy = false;
                            }
                            
                            pthread_rwlock_unlock(args->workers_list_rwlock);
                            
                            // Señalizar scheduler: worker disponible para nueva asignación
                            if (args->scheduler_mutex && args->scheduler_cond) {
                                pthread_mutex_lock(args->scheduler_mutex);
                                pthread_cond_signal(args->scheduler_cond);
                                pthread_mutex_unlock(args->scheduler_mutex);
                            }
                        }

                        // Marcar session como inactiva
                        if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
                        {
                            pthread_rwlock_wrlock(args->query_sessions_rwlock);
                            for (int i = 0; i < list_size(args->query_sessions); i++)
                            {
                                t_query_session *sess = list_get(args->query_sessions, i);
                                if (sess == finished_session)
                                {
                                    list_remove(args->query_sessions, i);
                                    break;
                                }
                            }
                            
                            // Remover del hash map
                            char key[16];
                            snprintf(key, sizeof(key), "%u", finished_session->query_id);
                            dictionary_remove(args->queries_by_id, key);
                            
                            pthread_rwlock_unlock(args->query_sessions_rwlock);
                        }
                        free_query_session(finished_session);
                    }
                    else
                    {
                        log_error(logger, "[MASTER] ERROR: No se encontró query activa para Worker %u (socket %d)", 
                                  client->worker_id, client->socket_conn);
                    }
                }
                else
                {
                    log_error(logger, "[MASTER] ERROR: Fallo al recibir OP_WORKER_QUERY_FINISHED del Worker %u", client->worker_id);
                }
            }
            
            // Re-peek nuevamente para verificar si hay OP_WORKER_QUERY_ERROR
            peek_res = recv(client->socket_conn, &op_peek, 1, MSG_PEEK | MSG_DONTWAIT);
            
            // Worker notifica que la query terminó con ERROR
            if (peek_res > 0 && op_peek == OP_WORKER_QUERY_ERROR)
            {
                uint32_t error_query_id = 0;
                char *error_msg = NULL;
                
                log_warning(logger, "[MASTER] Recibido OP_WORKER_QUERY_ERROR del Worker %u", client->worker_id);
                
                // protocol_recv_worker_query_error() consume el opcode internamente
                if (protocol_recv_worker_query_error(client->socket_conn, &error_query_id, &error_msg) == 0)
                {
                    // Buscar la session por el socket del worker
                    t_query_session *error_session = NULL;

                    if (args->query_sessions && args->query_sessions_rwlock)
                    {
                        pthread_rwlock_rdlock(args->query_sessions_rwlock);
                        
                        for (int i = 0; i < list_size(args->query_sessions); i++)
                        {
                            t_query_session *sess = list_get(args->query_sessions, i);
                            
                            if (sess && sess->worker_socket == client->socket_conn && sess->is_active)
                            {
                                error_session = sess;
                                break;
                            }
                        }
                        pthread_rwlock_unlock(args->query_sessions_rwlock);
                    }

                    if (error_session)
                    {
                        // Enviar notificación de ERROR al QueryControl
                        const char *result_msg = error_msg ? error_msg : "ERROR: Query falló en Worker";
                        int send_result = protocol_send_query_result(error_session->querycontrol_socket,
                                                    error_session->query_id,
                                                    result_msg);
                        
                        if (send_result != 0) {
                            log_error(logger, "[✗] Query %u: FALLO al enviar resultado ERROR al Query Control (socket %d)", 
                                     error_session->query_id, 
                                     error_session->querycontrol_socket);
                            
                            if (error_msg) free(error_msg);
                            continue;
                        }
                        
                        // Log obligatorio: Finalización de Query con ERROR
                        log_error(logger, 
                            "## Se terminó la Query %u en el Worker %u - Motivo: ERROR (%s)",
                            error_session->query_id,
                            client->worker_id,
                            result_msg);
                        log_error(logger, "\x1b[31m[✗] Query %u: ERROR enviado al Query Control\x1b[0m",
                                 error_session->query_id);

                        // Marcar worker como disponible nuevamente
                        if (args->workers_by_socket && args->workers_list_rwlock)
                        {
                            pthread_rwlock_wrlock(args->workers_list_rwlock);
                            
                            char key[16];
                            snprintf(key, sizeof(key), "%d", client->socket_conn);
                            t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                            
                            if (winfo)
                            {
                                winfo->is_busy = false;
                            }
                            
                            pthread_rwlock_unlock(args->workers_list_rwlock);
                            
                            // Señalizar scheduler: worker disponible
                            if (args->scheduler_mutex && args->scheduler_cond) {
                                pthread_mutex_lock(args->scheduler_mutex);
                                pthread_cond_signal(args->scheduler_cond);
                                pthread_mutex_unlock(args->scheduler_mutex);
                            }
                        }

                        // Remover session de la lista
                        if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
                        {
                            pthread_rwlock_wrlock(args->query_sessions_rwlock);
                            for (int i = 0; i < list_size(args->query_sessions); i++)
                            {
                                t_query_session *sess = list_get(args->query_sessions, i);
                                if (sess == error_session)
                                {
                                    list_remove(args->query_sessions, i);
                                    break;
                                }
                            }
                            
                            // Remover del hash map
                            char key[16];
                            snprintf(key, sizeof(key), "%u", error_session->query_id);
                            dictionary_remove(args->queries_by_id, key);
                            
                            pthread_rwlock_unlock(args->query_sessions_rwlock);
                        }
                        free_query_session(error_session);
                    }
                    else
                    {
                        log_error(logger, "[MASTER] ERROR: No se encontró query activa para Worker %u (socket %d) - OP_WORKER_QUERY_ERROR", 
                                  client->worker_id, client->socket_conn);
                    }
                    
                    if (error_msg) free(error_msg);
                }
                else
                {
                    log_error(logger, "[MASTER] ERROR: Fallo al recibir OP_WORKER_QUERY_ERROR del Worker %u", client->worker_id);
                }
            }
            //AGREGUE
else if (peek_res > 0 && op_peek == OP_WORKER_EVICT_ACK)
{
    uint32_t ack_query_id = 0;
    uint32_t ack_version  = 0;
    uint32_t ack_pc       = 0;

    if (protocol_recv_worker_evict_ack(client->socket_conn,
                                       &ack_query_id,
                                       &ack_pc,
                                       &ack_version) == 0)
    {
        // Buscar sesión
        t_query_session *sess = NULL;
        if (args->queries_by_id && args->query_sessions_rwlock) {
            pthread_rwlock_rdlock(args->query_sessions_rwlock);
            char key[16];
            snprintf(key, sizeof(key), "%u", ack_query_id);
            sess = (t_query_session *)dictionary_get(args->queries_by_id, key);
            pthread_rwlock_unlock(args->query_sessions_rwlock);
        }

        // Validar versión y estado antes de aplicar
        if (sess && ack_version == sess->version &&
            !sess->is_active && sess->worker_socket == -1)
        {
            pthread_rwlock_wrlock(args->query_sessions_rwlock);
            sess->pc = ack_pc;
            atomic_store(&sess->ready_since, time(NULL));
            pthread_rwlock_unlock(args->query_sessions_rwlock);

            log_info(logger,
                     "[✓] Recibido EVICT_ACK válido para Query %u versión %u (pc=%u)",
                     ack_query_id, ack_version, ack_pc);

            // Limpiar estado del worker
            if (args->workers_by_socket && args->workers_list_rwlock) {
                pthread_rwlock_wrlock(args->workers_list_rwlock);
                char wkey[16];
                snprintf(wkey, sizeof(wkey), "%d", client->socket_conn);
                t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, wkey);
                if (winfo) {
                    winfo->awaiting_evict_response = false;
                    winfo->evict_request_time = 0;
                    winfo->is_busy = false;
                    log_info(logger, "[EVICT_ACK] Worker %u liberado tras ACK", winfo->worker_id);
                    
                    // Log obligatorio: Desalojo exitoso por PRIORIDAD
                    log_info(logger, 
                        "\x1b[1;32m## Se desaloja la Query %u (%u) del Worker %u - Motivo: PRIORIDAD\x1b[0m",
                        ack_query_id, sess->priority, winfo->worker_id);
                }
                pthread_rwlock_unlock(args->workers_list_rwlock);
            }

            // Señalizar scheduler
            if (args->scheduler_mutex && args->scheduler_cond) {
                pthread_mutex_lock(args->scheduler_mutex);
                pthread_cond_signal(args->scheduler_cond);
                pthread_mutex_unlock(args->scheduler_mutex);
            }
        }
        else {
            log_warning(logger,
                        "[✗] EVICT_ACK ignorado: versión desactualizada o sesión no válida (Query %u, versión %u)",
                        ack_query_id, ack_version);
        }
    }
}


            //AGREGUE
        }

        // TOBI, ESTA LINEA METE CPU 100%, HACER SLEEP
        usleep(200000); // 200ms para no saturar CPU
    }

    // ===== MANEJO DE DESCONEXIÓN =====
    
    // Si es un Query Control que se desconectó
    if (client->type == CLIENT_TYPE_QUERY_CONTROL)
    {
        // Buscar la query asociada a este QueryControl
        t_query_session *disconnected_session = NULL;
        
        if (args->query_sessions && args->query_sessions_rwlock)
        {
            pthread_rwlock_rdlock(args->query_sessions_rwlock);
            for (int i = 0; i < list_size(args->query_sessions); i++)
            {
                t_query_session *sess = list_get(args->query_sessions, i);
                if (sess && sess->querycontrol_socket == client->socket_conn)
                {
                    disconnected_session = sess;
                    break;
                }
            }
            pthread_rwlock_unlock(args->query_sessions_rwlock);
        }
        
        if (disconnected_session)
        {
            // Obtener nivel de multiprocesamiento actual
            int current_workers = 0;
            if (args->worker_count && args->worker_count_mutex)
            {
                pthread_mutex_lock(args->worker_count_mutex);
                current_workers = *args->worker_count;
                pthread_mutex_unlock(args->worker_count_mutex);
            }
            
            // Log obligatorio: Desconexión de Query Control
            t_metadata *m = metadata_create(LOG_CORE);
            logger_info(
                logger,
                string_from_format(
                    "## Se desconecta un Query Control. Se finaliza la Query %u con prioridad %u. Nivel multiprocesamiento %d",
                    disconnected_session->query_id,
                    disconnected_session->priority,
                    current_workers
                ),
                m
            );
            metadata_clear(m);
            
            // Si la query está en ejecución (EXEC), debemos notificar al Worker
            uint32_t worker_id_for_log = 0;
            if (disconnected_session->is_active && disconnected_session->worker_socket > 0)
            {
                // TODO: Implementar OP_MASTER_CANCEL para desalojar query
                // Por ahora solo marcamos como inactiva
                // En una implementación completa:
                // protocol_send_master_cancel(disconnected_session->worker_socket, disconnected_session->query_id);
                
                // Liberar el worker con hash map O(1)
                if (args->workers_by_socket && args->workers_list_rwlock)
                {
                    pthread_rwlock_rdlock(args->workers_list_rwlock);
                    
                    char key[16];
                    snprintf(key, sizeof(key), "%d", disconnected_session->worker_socket);
                    t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                    
                    if (winfo)
                    {
                        worker_id_for_log = winfo->worker_id;
                    }
                    
                    pthread_rwlock_unlock(args->workers_list_rwlock);
                }
            }
            if (protocol_send_master_evict(disconnected_session->worker_socket,disconnected_session->query_id, disconnected_session->version) == 0)
                {
                    uint32_t evicted_id = 0, evicted_pc = 0, evicted_version=0;
                    if (protocol_recv_worker_evict_ack(disconnected_session->worker_socket, &evicted_id, &evicted_pc, &evicted_version) == 0)
                    {
                        // Log obligatorio: desalojo por desconexión
                        t_metadata *md = metadata_create(LOG_CORE);
                        logger_info(
                            logger,
                            string_from_format(
                                "## Se desaloja la Query %u (%u) del Worker %u - Motivo: DESCONEXION",
                                disconnected_session->query_id,
                                disconnected_session->priority,
                                worker_id_for_log
                            ),
                            md
                        );
                        metadata_clear(md);                
                        } else {
                             // Si el Worker no devuelve el ack, igual se libera su estado con hash map O(1)
                            if (args->workers_by_socket && args->workers_list_rwlock) {
                                pthread_rwlock_wrlock(args->workers_list_rwlock);
                                
                                char key[16];
                                snprintf(key, sizeof(key), "%d", disconnected_session->worker_socket);
                                t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                                
                                if (winfo) {
                                    winfo->is_busy = false;
                                }
                                
                                pthread_rwlock_unlock(args->workers_list_rwlock);
                            }
                            t_metadata *me = metadata_create(LOG_CORE);
                            logger_warning(logger, "No se recibió ack de evict del Worker", me);
                            metadata_clear(me);
                    }
                }

            if (args->workers_by_socket && args->workers_list_rwlock)
            {
                pthread_rwlock_wrlock(args->workers_list_rwlock);
                
                char key[16];
                snprintf(key, sizeof(key), "%d", disconnected_session->worker_socket);
                t_worker_info *winfo = (t_worker_info *)dictionary_get(args->workers_by_socket, key);
                
                if (winfo)
                {
                    winfo->is_busy = false;
                }
                
                pthread_rwlock_unlock(args->workers_list_rwlock);
        }
            
            // Remover la session de la lista
            if (args->query_sessions && args->query_sessions_rwlock && args->queries_by_id)
            {
                pthread_rwlock_wrlock(args->query_sessions_rwlock);
                for (int i = 0; i < list_size(args->query_sessions); i++)
                {
                    t_query_session *sess = list_get(args->query_sessions, i);
                    if (sess == disconnected_session)
                    {
                        list_remove(args->query_sessions, i);
                        break;
                    }
                }
                
                // Remover del hash map
                char key[16];
                snprintf(key, sizeof(key), "%u", disconnected_session->query_id);
                dictionary_remove(args->queries_by_id, key);
                
                pthread_rwlock_unlock(args->query_sessions_rwlock);
            }
            free_query_session(disconnected_session);
    }

}
    log_trace(logger, "[✓] Finalizando hilo para cliente persistente: %d", client->client_id);

    free(args);
    return NULL;
}
/* ========================================================================== */
/*                     COMPARADOR PARA SCHEDULER DE PRIORIDADES               */
/* ========================================================================== */

/**
 * @brief Comparador para ordenar queries por prioridad (menor valor = mayor prioridad)
 * @param a Primera query session
 * @param b Segunda query session 
 * @return Negativo si a < b, positivo si a > b, 0 si iguales
 * @note Menor número = mayor prioridad (1 es mayor prioridad que 5)
 */
bool priority_comparator(void *a, void *b)
{
    t_query_session *sess_a = (t_query_session *)a;
    t_query_session *sess_b = (t_query_session *)b;
    
    if (!sess_a || !sess_b) return false;
    
    // Menor priority numérico = mayor prioridad real
    return sess_a->priority < sess_b->priority;
}

/* ========================================================================== */
/*                     SCHEDULER CON PRIORIDADES Y PREEMPTION                 */
/* ========================================================================== */

/**
 * @brief Thread del scheduler con prioridades y desalojo (preemption)
 * @param arg Puntero a t_scheduler_args con los recursos compartidos
 * @return NULL
 * @note Implementa algoritmo de prioridades con preemption y actualización de ready_since
 */
void *scheduler_priority_thread(void *arg)
{
    t_scheduler_args *args = (t_scheduler_args *)arg;

    if (!args)
    {
        return NULL;
    }
    
    t_log *logger = args->logger;
    t_list *query_sessions = args->query_sessions;
    pthread_rwlock_t *query_sessions_rwlock = args->query_sessions_rwlock;
    t_list *workers_list = args->workers_list;
    pthread_rwlock_t *workers_list_rwlock = args->workers_list_rwlock;
    pthread_cond_t *scheduler_cond = args->scheduler_cond;
    pthread_mutex_t *scheduler_mutex = args->scheduler_mutex;
    
    // Extraer para uso dentro del scheduler
    t_scheduler_args *scheduler_args = args;

    log_info(logger, "[✓] Iniciando master con el algoritmo de PRIORIDADES");

    while (true)
    {
        // Esperar señalización reactiva con timeout corto para responsiveness
        pthread_mutex_lock(scheduler_mutex);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 100000000; // 100ms timeout (más responsive)
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec += 1;
            timeout.tv_nsec -= 1000000000;
        }
        
        int wait_result = pthread_cond_timedwait(scheduler_cond, scheduler_mutex, &timeout);
        pthread_mutex_unlock(scheduler_mutex);
        
        // Logs removidos para no saturar el log con mensajes cada 100ms

        t_query_session *highest_priority_query = NULL;
        t_query_session *active_query_to_evict = NULL;
        t_worker_info *worker_to_evict_from = NULL;

        // FASES 1 y 2 CONSOLIDADAS: Leer queries y workers en un solo bloque crítico
        // Esto reduce overhead de sincronización sin aumentar contención (ambos son rdlocks)
        t_worker_info *available_worker = NULL;
        bool has_available_worker = false;
        int ready_queries_count = 0;
        int available_workers_count = 0;
        
        if (query_sessions && query_sessions_rwlock && workers_list && workers_list_rwlock)
        {
            // Adquirir ambos rdlocks - no hay riesgo de deadlock (siempre mismo orden)
            pthread_rwlock_rdlock(query_sessions_rwlock);
            pthread_rwlock_rdlock(workers_list_rwlock);
            
            // FASE 1: Buscar query más prioritaria READY
            list_sort(query_sessions, priority_comparator);
            
            for (int i = 0; i < list_size(query_sessions); i++)
            {
                t_query_session *sess = list_get(query_sessions, i);
                if (sess && !sess->is_active)
                {
                    ready_queries_count++;
                    if (!highest_priority_query) {
                        highest_priority_query = sess;
                    }
                }
            }
            
            // FASE 2: Buscar worker disponible (mientras tenemos ambos locks)
            for (int i = 0; i < list_size(workers_list); i++)
            {
                t_worker_info *winfo = list_get(workers_list, i);
                if (winfo && !winfo->is_busy)
                {
                    available_workers_count++;
                    if (!has_available_worker) {
                        has_available_worker = true;
                        available_worker = winfo;
                    }
                }
            }
            
            // Liberar ambos locks simultáneamente
            pthread_rwlock_unlock(workers_list_rwlock);
            pthread_rwlock_unlock(query_sessions_rwlock);
        }

        // Log diagnóstico solo si hay actividad (no cada ciclo vacío)
        if (ready_queries_count > 0 || available_workers_count > 0) {
            log_debug(logger, "[SCHEDULER] Estado: %d queries READY, %d workers DISPONIBLES", 
                     ready_queries_count, available_workers_count);
        }

        // Si no hay queries pendientes, continuar
        if (!highest_priority_query)
        {
            continue;
        }
        
        log_debug(logger, "[SCHEDULER] Query %u (prioridad %u) está READY - buscando asignación", 
                highest_priority_query->query_id, highest_priority_query->priority);

        // FASE 3: Solo desalojar si NO hay Workers disponibles
        int evict_worker_socket = -1;
        uint32_t evict_query_id = 0;
        
        // Solo considerar desalojo si NO hay workers disponibles
        if (!has_available_worker && query_sessions && query_sessions_rwlock && workers_list && workers_list_rwlock)
        {
            // Consolidar: adquirir ambos rdlocks para buscar víctima de desalojo
            pthread_rwlock_rdlock(query_sessions_rwlock);
            pthread_rwlock_rdlock(workers_list_rwlock);
            
            // Buscar query activa con menor prioridad (mayor número)
            for (int i = 0; i < list_size(query_sessions); i++)
            {
                t_query_session *sess = list_get(query_sessions, i);
                if (sess && sess->is_active && sess->priority > highest_priority_query->priority)
                {
                    active_query_to_evict = sess;
                    evict_worker_socket = sess->worker_socket;
                    evict_query_id = sess->query_id;
                    break;
                }
            }
            
            // Buscar worker asociado con hash map O(1) (mientras tenemos ambos locks)
            if (active_query_to_evict && evict_worker_socket > 0 && scheduler_args->workers_by_socket)
            {
                char key[16];
                snprintf(key, sizeof(key), "%d", evict_worker_socket);
                worker_to_evict_from = (t_worker_info *)dictionary_get(scheduler_args->workers_by_socket, key);
            }
            
            // Liberar ambos locks
            pthread_rwlock_unlock(workers_list_rwlock);
            pthread_rwlock_unlock(query_sessions_rwlock);
            
            //AGREGUE
if (worker_to_evict_from)
{
    pthread_rwlock_wrlock(query_sessions_rwlock);
    active_query_to_evict->version++;             
    active_query_to_evict->is_active = false;
    active_query_to_evict->estado = READY;
    active_query_to_evict->worker_socket = -1;
    atomic_store(&active_query_to_evict->ready_since, time(NULL));
    pthread_rwlock_unlock(query_sessions_rwlock);

    pthread_rwlock_wrlock(workers_list_rwlock);
    worker_to_evict_from->awaiting_evict_response = true;
    worker_to_evict_from->evict_request_time = time(NULL);
    pthread_rwlock_unlock(workers_list_rwlock);

    if (protocol_send_master_evict(worker_to_evict_from->socket_conn,
                                   evict_query_id,
                                   active_query_to_evict->version) == 0)
    {
        log_info(logger, "[EVICT] Solicitud enviada a Worker %u para Query %u versión %u",
                 worker_to_evict_from->worker_id,
                 evict_query_id,
                 active_query_to_evict->version);

        if (scheduler_args->scheduler_mutex && scheduler_args->scheduler_cond) {
            pthread_mutex_lock(scheduler_args->scheduler_mutex);
            pthread_cond_signal(scheduler_args->scheduler_cond);
            pthread_mutex_unlock(scheduler_args->scheduler_mutex);
        }
    }
    else
    {
        log_error(logger, "[EVICT] ✗ Error al enviar solicitud de desalojo al Worker %u",
                 worker_to_evict_from->worker_id);

        pthread_rwlock_wrlock(workers_list_rwlock);
        worker_to_evict_from->awaiting_evict_response = false;
        worker_to_evict_from->evict_request_time = 0;
        pthread_rwlock_unlock(workers_list_rwlock);

        if (scheduler_args->scheduler_mutex && scheduler_args->scheduler_cond) {
            pthread_mutex_lock(scheduler_args->scheduler_mutex);
            pthread_cond_signal(scheduler_args->scheduler_cond);
            pthread_mutex_unlock(scheduler_args->scheduler_mutex);
        }
    }
}


            //AGREGUE
/*
            // DESALOJO: enviar OP_MASTER_EVICT
            if (worker_to_evict_from)
                {
                    if (protocol_send_master_evict(worker_to_evict_from->socket_conn, 
                                                   evict_query_id) == 0)
                    {
                        // Esperar ACK con PC actualizado (timeout de 5 segundos)
                        uint32_t evicted_query_id = 0;
                        uint32_t evicted_pc = 0;
                        
                        int evict_result = protocol_recv_worker_evict_ack_timeout(
                            worker_to_evict_from->socket_conn,
                            &evicted_query_id, 
                            &evicted_pc,
                            30000  // 30 segundos timeout (ajustado para soportar retardos en Storage)
                        );
                        
                        if (evict_result == 0)
                        {
                            // ACK recibido exitosamente - Consolidar actualización de query y worker
                            pthread_rwlock_wrlock(query_sessions_rwlock);
                            pthread_rwlock_wrlock(workers_list_rwlock);
                            
                            // Actualizar la query desalojada
                            active_query_to_evict->is_active = false;
                            active_query_to_evict->pc = evicted_pc;
                            atomic_store(&active_query_to_evict->ready_since, time(NULL));
                            active_query_to_evict->worker_socket = -1;
                            
                            // Liberar el worker y limpiar flag de awaiting_evict_response
                            worker_to_evict_from->is_busy = false;
                            worker_to_evict_from->awaiting_evict_response = false;
                            worker_to_evict_from->evict_request_time = 0;
                            
                            pthread_rwlock_unlock(workers_list_rwlock);
                            pthread_rwlock_unlock(query_sessions_rwlock);
                            
                            // Log obligatorio: Desalojo exitoso por PRIORIDAD
                            log_info(logger, 
                                "## Se desaloja la Query %u (%u) del Worker %u - Motivo: PRIORIDAD",
                                evicted_query_id, active_query_to_evict->priority, worker_to_evict_from->worker_id);
                            
                            // Señalizar scheduler: query desalojada retornó a READY con worker disponible
                            if (scheduler_args->scheduler_mutex && scheduler_args->scheduler_cond) {
                                pthread_mutex_lock(scheduler_args->scheduler_mutex);
                                pthread_cond_signal(scheduler_args->scheduler_cond);
                                pthread_mutex_unlock(scheduler_args->scheduler_mutex);
                            }
                        }
                        else
                        {
                            // Timeout o error: el Worker no respondió
                            log_error(logger, "[EVICT] ✗ Timeout esperando ACK de Worker %u para Query %u - Worker marcado como problemático",
                                     worker_to_evict_from->worker_id, evict_query_id);
                            
                            // CRÍTICO: NO liberar el worker - sigue ocupado procesando la query original
                            // Marcar que está esperando respuesta de EVICT para que Watchdog no lo libere
                            pthread_rwlock_wrlock(workers_list_rwlock);
                            worker_to_evict_from->awaiting_evict_response = true;
                            worker_to_evict_from->evict_request_time = time(NULL); // Timestamp del EVICT
                            pthread_rwlock_unlock(workers_list_rwlock);
                            
                            // Si lo liberamos, FASE 4 lo reasignará inmediatamente creando race condition
                            pthread_rwlock_wrlock(query_sessions_rwlock);
                            
                            // Solo liberar la query (el worker sigue busy hasta que responda o se desconecte)
                            active_query_to_evict->is_active = false;
                            active_query_to_evict->worker_socket = -1;
                            atomic_store(&active_query_to_evict->ready_since, time(NULL));
                            
                            pthread_rwlock_unlock(query_sessions_rwlock);
                            
                            log_warning(logger, "[EVICT] Query %u retornada a estado READY - Worker %u sigue ocupado (awaiting_evict_response=true)",
                                       evict_query_id, worker_to_evict_from->worker_id);
                        }
                    }
                    else
                    {
                        log_error(logger, "[EVICT] ✗ Error al enviar solicitud de desalojo al Worker %u",
                                 worker_to_evict_from->worker_id);
                    }
                }
     
*/     
     
     
        }  // Cierre del if (!has_available_worker ...) de FASE 3

        // FASE 4: Asignar query más prioritaria a worker disponible
        // Si hubo desalojo, buscar nuevamente el worker (que quedó libre)
        if (!has_available_worker && workers_list && workers_list_rwlock)
        {
            // Si NO había worker disponible al inicio, buscar de nuevo (puede que se liberó uno por desalojo)
            pthread_rwlock_rdlock(workers_list_rwlock);
            for (int i = 0; i < list_size(workers_list); i++)
            {
                t_worker_info *winfo = list_get(workers_list, i);
                if (winfo && !winfo->is_busy)
                {
                    available_worker = winfo;
                    break;
                }
            }
            pthread_rwlock_unlock(workers_list_rwlock);
        }

        // Si hay worker disponible, asignar la query prioritaria
        if (available_worker)
        {
            // Consolidar: Adquirir ambos wrlocks para actualizar worker y query atómicamente
            pthread_rwlock_wrlock(workers_list_rwlock);
            pthread_rwlock_wrlock(query_sessions_rwlock);
            
            // Marcar worker como ocupado
            available_worker->is_busy = true;
            
            // CRÍTICO: Revalidar que highest_priority_query sigue siendo válida
            bool query_still_valid = false;
            for (int i = 0; i < list_size(query_sessions); i++)
            {
                t_query_session *sess = list_get(query_sessions, i);
                if (sess == highest_priority_query && !sess->is_active)
                {
                    query_still_valid = true;
                    break;
                }
            }
            
            if (!query_still_valid)
            {
                // La query fue eliminada o ya está activa, liberar worker y continuar
                available_worker->is_busy = false;
                pthread_rwlock_unlock(query_sessions_rwlock);
                pthread_rwlock_unlock(workers_list_rwlock);
                continue;
            }
            
            // Query válida, marcarla como activa ANTES de enviar
            highest_priority_query->is_active = true;
            highest_priority_query->worker_socket = available_worker->socket_conn;
            
            // Copiar datos necesarios mientras tenemos los locks
            uint32_t query_id = highest_priority_query->query_id;
            uint32_t priority = highest_priority_query->priority;
            uint32_t pc = highest_priority_query->pc;
            char *query_path_copy = strdup(highest_priority_query->query_path);
            
            pthread_rwlock_unlock(query_sessions_rwlock);
            pthread_rwlock_unlock(workers_list_rwlock);
            
            // Ahora enviar sin mutex (protocolo puede ser lento)
            if (protocol_send_master_path(available_worker->socket_conn,
                                          query_id,
                                          query_path_copy,
                                          pc) == 0)
            {

                // Log obligatorio: Asignación de query
                log_info(
                    logger,
                    "## Se envía la Query %u (%u) al Worker %u",
                    highest_priority_query->query_id,
                    highest_priority_query->priority,
                    available_worker->worker_id
                );
                log_info(logger, "\x1b[32m[✓] Query %u: Se le asignó al Worker <%u> la ejecución de esta instrucción\x1b[0m",
                         highest_priority_query->query_id, available_worker->worker_id);
            }
            else
            {
                // Si falla el envío, revertir estado consolidando ambos wrlocks
                pthread_rwlock_wrlock(query_sessions_rwlock);
                pthread_rwlock_wrlock(workers_list_rwlock);
                
                // Revalidar que la query sigue existiendo antes de modificarla
                bool query_exists = false;
                for (int i = 0; i < list_size(query_sessions); i++)
                {
                    t_query_session *sess = list_get(query_sessions, i);
                    if (sess == highest_priority_query)
                    {
                        sess->is_active = false;
                        sess->worker_socket = -1;
                        query_exists = true;
                        break;
                    }
                }
                
                available_worker->is_busy = false;
                
                pthread_rwlock_unlock(workers_list_rwlock);
                pthread_rwlock_unlock(query_sessions_rwlock);
            }
            
            free(query_path_copy);
        }
    }

    return NULL;
}

/* ========================================================================== */
/*                     AGING THREAD (ENVEJECIMIENTO DE PRIORIDADES)           */
/* ========================================================================== */

/**
 * @brief Thread de aging que incrementa la prioridad de queries READY
 * @param arg Puntero a t_scheduler_args con los recursos compartidos
 * @return NULL
 * @note Se ejecuta cada TIEMPO_ENVEJECIMIENTO milisegundos
 * @note Solo decrementa priority si es > 1 (1 es la máxima prioridad)
 */
void *aging_thread(void *arg)
{
    t_scheduler_args *args = (t_scheduler_args *)arg;

    if (!args)
    {
        return NULL;
    }

    t_log *logger = args->logger;
    t_list *query_sessions = args->query_sessions;
    pthread_rwlock_t *query_sessions_rwlock = args->query_sessions_rwlock;
    int tiempo_aging_ms = args->tiempo_aging_ms;
    
    t_metadata *meta = metadata_create(LOG_CORE);
    metadata_add(meta, "TIEMPO_AGING_ms", string_from_format("%d", tiempo_aging_ms));
    logger_info(logger, "## Aging Thread iniciado", meta);
    metadata_clear(meta);

    while (true)
    {
        // Dormir por TIEMPO_AGING milisegundos
        usleep(tiempo_aging_ms * 1000); // convertir ms a microsegundos
        
        if (query_sessions && query_sessions_rwlock)
        {
            pthread_rwlock_wrlock(query_sessions_rwlock);  // wrlock porque modificamos priority y list_sort
            
            time_t current_time = time(NULL);
            int queries_aged = 0;
            
            // Iterar todas las queries
            for (int i = 0; i < list_size(query_sessions); i++)
            {
                t_query_session *sess = list_get(query_sessions, i);
                
                if (sess && !sess->is_active && sess->priority > 1)
                {
                    // Calcular tiempo en READY (en segundos) - lectura atómica
                    time_t ready_time = atomic_load(&sess->ready_since);
                    double time_in_ready = difftime(current_time, ready_time);
                    
                    // Convertir TIEMPO_AGING de ms a segundos para comparar
                    double aging_threshold_sec = tiempo_aging_ms / 1000.0;
                    
                    if (time_in_ready >= aging_threshold_sec)
                    {
                        sess->priority--; // Incrementar prioridad (menor número)
                        atomic_store(&sess->ready_since, current_time); // Reiniciar contador atómicamente
                        queries_aged++;
                        
                        // Log obligatorio: Cambio de prioridad
                        log_info(logger, 
                            "## %u Cambio de prioridad: %u - %u",
                            sess->query_id,
                            sess->priority + 1,  // Prioridad anterior
                            sess->priority);     // Prioridad nueva
                    }
                }
            }
            
            // CRÍTICO: Reordenar lista si hubo cambios de prioridad
            if (queries_aged > 0)
            {
                list_sort(query_sessions, priority_comparator);
                
                // Señalizar scheduler: prioridades cambiaron, re-evaluar asignaciones
                if (args->scheduler_mutex && args->scheduler_cond) {
                    pthread_mutex_lock(args->scheduler_mutex);
                    pthread_cond_signal(args->scheduler_cond);
                    pthread_mutex_unlock(args->scheduler_mutex);
                }
            }
            
            pthread_rwlock_unlock(query_sessions_rwlock);
            
            if (queries_aged > 0)
            {
                t_metadata *m = metadata_create(LOG_CORE);
                metadata_add(m, "Queries_Envejecidas", string_from_format("%d", queries_aged));
                logger_info(logger, "## Ciclo de Aging completado", m);
                metadata_clear(m);
            }
        }
    }

    return NULL;
}

/* ========================================================================== */
/*                     SCHEDULER FIFO (ORIGINAL)                              */
/* ========================================================================== */

void *scheduler_thread(void *arg)
{
    t_scheduler_args *args = (t_scheduler_args *)arg;

    if (!args)
    {
        return NULL;
    }

    t_log *logger = args->logger;
    t_list *query_sessions = args->query_sessions;
    pthread_rwlock_t *query_sessions_rwlock = args->query_sessions_rwlock;
    t_list *workers_list = args->workers_list;
    pthread_rwlock_t *workers_list_rwlock = args->workers_list_rwlock;

    while (true)
    {
        usleep(100000); // 100ms - dar más tiempo a threads de mensajes

        // Buscar una query pendiente (is_active=false)
        t_query_session *pending_query = NULL;

        if (query_sessions && query_sessions_rwlock)
        {
            pthread_rwlock_rdlock(query_sessions_rwlock);
            for (int i = 0; i < list_size(query_sessions); i++)
            {
                t_query_session *sess = list_get(query_sessions, i);
                if (sess && !sess->is_active)
                {
                    pending_query = sess;
                    break; // FIFO: tomamos la primera pendiente
                }
            }
            pthread_rwlock_unlock(query_sessions_rwlock);
        }

        // Si NO hay query pendiente, continuar
        if (!pending_query)
        {
            continue;
        }
        
        // Si hay query pendiente, buscar worker disponible
        t_worker_info *available_worker = NULL;

        if (workers_list && workers_list_rwlock)
        {
            pthread_rwlock_wrlock(workers_list_rwlock);
            for (int i = 0; i < list_size(workers_list); i++)
            {
                t_worker_info *winfo = list_get(workers_list, i);
                if (winfo && !winfo->is_busy)
                {
                    available_worker = winfo;
                    winfo->is_busy = true; // Marcar como ocupado
                    break;                 // FIFO: tomamos el primer worker disponible
                }
            }
            pthread_rwlock_unlock(workers_list_rwlock);
        }

        // Si hay worker disponible, asignar la query
        if (available_worker)
        {
            // Asignar query al worker
            if (protocol_send_master_path(available_worker->socket_conn,
                                          pending_query->query_id,
                                          pending_query->query_path,
                                          pending_query->pc) == 0)
            {
                // Marcar query como activa
                pthread_rwlock_wrlock(query_sessions_rwlock);
                pending_query->is_active = true;
                pending_query->worker_socket = available_worker->socket_conn;
                pthread_rwlock_unlock(query_sessions_rwlock);

                // Log obligatorio: Asignación de query
                log_info(
                    logger,
                    "## Se envía la Query %u (%u) al Worker %u",
                    pending_query->query_id,
                    pending_query->priority,
                    available_worker->worker_id
                );
                log_info(logger, "\x1b[32m[✓] Query %u: Se le asignó al Worker <%u> la ejecución de esta instrucción\x1b[0m",
                         pending_query->query_id, available_worker->worker_id);
            }
            else
            {
                // Si falla el envío, liberar el worker
                pthread_rwlock_wrlock(workers_list_rwlock);
                available_worker->is_busy = false;
                pthread_rwlock_unlock(workers_list_rwlock);
            }
        }
        else
        {
            // Hay query pendiente pero no hay workers disponibles
            static uint32_t last_logged_query = 0;
            if (last_logged_query != pending_query->query_id) {
                log_debug(logger, "[SCHEDULER FIFO] Query %u está esperando un Worker disponible...", pending_query->query_id);
                last_logged_query = pending_query->query_id;
            }
        }
    }

    return NULL;
}

/* ========================================================================== */
/*                          WATCHDOG THREAD (RESILIENCE)                      */
/* ========================================================================== */

/**
 * @brief Thread de vigilancia que detecta queries/workers colgadas
 * 
 * Responsabilidades:
 * 1. Detectar queries activas sin progreso durante query_timeout_sec
 * 2. Marcar workers como disponibles si su query desaparece
 * 3. Log de queries READY esperando mucho tiempo (métrica de observabilidad)
 * 
 * @param arg t_watchdog_args* con configuración del watchdog
 */
void* watchdog_thread(void* arg) {
    t_watchdog_args* watchdog_args = (t_watchdog_args*)arg;
    
    if (!watchdog_args || !watchdog_args->logger) {
        return NULL;
    }
    
    t_log* logger = watchdog_args->logger;
    t_list* query_sessions = watchdog_args->query_sessions;
    pthread_rwlock_t* query_sessions_rwlock = watchdog_args->query_sessions_rwlock;
    t_list* workers_list = watchdog_args->workers_list;
    t_dictionary* workers_by_socket = watchdog_args->workers_by_socket;
    pthread_rwlock_t* workers_list_rwlock = watchdog_args->workers_list_rwlock;
    int check_interval = watchdog_args->check_interval_sec;
    int query_timeout = watchdog_args->query_timeout_sec;
    
    log_debug(logger, "[WATCHDOG] Thread iniciado - interval=%ds, query_timeout=%ds", 
             check_interval, query_timeout);
    
    while (true) {
        sleep(check_interval);
        
        time_t now = time(NULL);
        int ready_count = 0;
        int exec_count = 0;
        int stale_workers_fixed = 0;
        
        // FASE 1: Revisar queries activas por timeout
        if (query_sessions && query_sessions_rwlock) {
            pthread_rwlock_rdlock(query_sessions_rwlock);
            
            for (int i = 0; i < list_size(query_sessions); i++) {
                t_query_session* sess = list_get(query_sessions, i);
                if (!sess) continue;
                
                if (sess->is_active) {
                    exec_count++;
                    
                    // Detectar queries activas sin progreso (posible worker colgado)
                    time_t ready_time = atomic_load(&sess->ready_since);
                    int elapsed = (int)difftime(now, ready_time);
                    
                    if (elapsed > query_timeout) {
                        log_warning(logger, 
                            "[WATCHDOG] ⚠ Query %u activa por %ds (timeout=%ds) - Worker socket=%d - Posible bloqueo",
                            sess->query_id, elapsed, query_timeout, sess->worker_socket);
                        // TODO: Implementar recuperación automática (forzar eviction)
                    }
                } else {
                    ready_count++;
                }
            }
            
            pthread_rwlock_unlock(query_sessions_rwlock);
        }
        
        // FASE 2: Verificar workers ocupados sin query válida
        if (workers_list && workers_list_rwlock && workers_by_socket) {
            pthread_rwlock_wrlock(workers_list_rwlock);
            
            for (int i = 0; i < list_size(workers_list); i++) {
                t_worker_info* winfo = list_get(workers_list, i);
                if (!winfo || !winfo->is_busy) continue;
                
                // Verificar si existe una query activa usando este worker
                bool has_active_query = false;
                
                if (query_sessions && query_sessions_rwlock) {
                    pthread_rwlock_rdlock(query_sessions_rwlock);
                    
                    for (int j = 0; j < list_size(query_sessions); j++) {
                        t_query_session* sess = list_get(query_sessions, j);
                        if (sess && sess->is_active && sess->worker_socket == winfo->socket_conn) {
                            has_active_query = true;
                            break;
                        }
                    }
                    
                    pthread_rwlock_unlock(query_sessions_rwlock);
                }
                
                // Worker marcado como ocupado pero sin query activa -> corregir inconsistencia
                // PERO: NO liberar si está esperando respuesta de EVICT (SALVO que lleve demasiado tiempo)
                if (!has_active_query) {
                    if (winfo->awaiting_evict_response) {
                        // Chequear si el EVICT lleva demasiado tiempo sin respuesta
                        int evict_elapsed = (int)difftime(now, winfo->evict_request_time);
                        if (evict_elapsed > 40) {
                            // Más de 40 segundos esperando EVICT ACK - liberar forzosamente
                            log_warning(logger, 
                                "[WATCHDOG] ⚠ Worker %u (socket=%d) esperando EVICT ACK por %ds - Liberando forzosamente",
                                winfo->worker_id, winfo->socket_conn, evict_elapsed);
                            winfo->is_busy = false;
                            winfo->awaiting_evict_response = false;
                            winfo->evict_request_time = 0;
                            stale_workers_fixed++;
                        } else {
                            log_debug(logger, 
                                "[WATCHDOG] Worker %u (socket=%d) busy sin query activa pero awaiting_evict_response=true (%ds) - NO liberando aún",
                                winfo->worker_id, winfo->socket_conn, evict_elapsed);
                        }
                    } else {
                        log_warning(logger, 
                            "[WATCHDOG] ⚠ Worker %u (socket=%d) marcado ocupado sin query activa - Liberando",
                            winfo->worker_id, winfo->socket_conn);
                        winfo->is_busy = false;
                        stale_workers_fixed++;
                    }
                }
            }
            
            pthread_rwlock_unlock(workers_list_rwlock);
        }
    }
    
    return NULL;
}

/* ========================================================================== */
/*                     FUNCIONES DE CLEANUP DE RECURSOS                       */
/* ========================================================================== */

/**
 * @brief Libera una query_session y sus recursos asociados
 * @param session Puntero a la sesión a liberar (puede ser NULL)
 * @note Thread-safe: el caller debe tener el mutex de query_sessions si está en la lista
 */
void free_query_session(t_query_session* session) {
    if (!session) {
        return;
    }
    
    // Liberar el path si existe
    if (session->query_path) {
        free(session->query_path);
        session->query_path = NULL;
    }
    
    // Liberar la estructura
    free(session);
}

/**
 * @brief Libera un worker_info y sus recursos asociados
 * @param worker Puntero al worker_info a liberar (puede ser NULL)
 * @note Thread-safe: el caller debe tener el mutex de workers_list si está en la lista
 */
void free_worker_info(t_worker_info* worker) {
    if (!worker) {
        return;
    }
    
    // Por ahora solo liberamos la estructura
    // Si en el futuro worker_info tiene campos dinámicos, se liberarían aquí
    free(worker);
}