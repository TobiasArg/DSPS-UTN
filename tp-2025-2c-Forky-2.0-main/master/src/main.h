
#ifndef MASTER_H
#define MASTER_H

#include <utils/include.h>
#include <utils/parameters.h>
#include <utils/logger.h>
#include <utils/metadata.h>
#include <commons/string.h>
#include <utils/sockets.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <stdatomic.h>

typedef enum {
    CLIENT_TYPE_QUERY_CONTROL,
    CLIENT_TYPE_WORKER,
    CLIENT_TYPE_UNKNOWN
} client_type_t;

typedef struct {
    int puerto_master;
    char* ip_master;
    char* log_level;
    char* algoritmo_planning;
    int tiempo_aging;
} t_config_master;

typedef struct {
    int client_id;
    int socket_conn;
    client_type_t type;
    uint32_t worker_id; // Solo válido si type == CLIENT_TYPE_WORKER
} t_client_connection;
enum query_state { READY = 0, RUNNING = 1 };
typedef struct {
    uint32_t query_id;
    int querycontrol_socket;
    int worker_socket;
    char* query_path;
    uint32_t priority;
    uint32_t original_priority;  // Prioridad inicial (antes de aging)
    bool is_active;              // false = READY, true = EXEC
    uint32_t pc;                 // Program Counter (para reanudación tras desalojo)
    uint32_t version;   // para controlar ACKs de EVICT
    int estado;
    _Atomic(time_t) ready_since; // Timestamp cuando entró a READY (acceso atómico)
} t_query_session;


typedef struct {
    uint32_t worker_id;
    int socket_conn;
    bool is_busy;
    bool awaiting_evict_response; // Worker fue enviado EVICT y aún no respondió ACK
    time_t evict_request_time; // Timestamp cuando se envió EVICT (0 si no hay pedido pendiente)
} t_worker_info;

typedef struct {
    int socket_conn;
    client_type_t type;
    uint32_t worker_id; // Solo para workers
    bool is_active;
} t_client_info;

typedef struct {
    t_log* logger;
    t_client_connection* client;
    t_list* query_sessions;
    t_dictionary* queries_by_id;              // Hash map para búsqueda O(1) por query_id
    uint32_t* next_query_id;
    pthread_rwlock_t* query_sessions_rwlock;  // Cambiado a rwlock para lecturas concurrentes
    pthread_mutex_t* next_id_mutex;
    int* worker_count;
    pthread_mutex_t* worker_count_mutex;
    t_list *workers_list;
    t_dictionary* workers_by_socket;          // Hash map para búsqueda O(1) por socket
    pthread_rwlock_t* workers_list_rwlock;    // Cambiado a rwlock para lecturas concurrentes
    pthread_cond_t* scheduler_cond;           // Condition variable para señalización
    pthread_mutex_t* scheduler_mutex;         // Mutex asociado a condition variable
} t_client_handler_args;

typedef struct {
    t_log* logger;
    t_list* query_sessions;
    t_dictionary* queries_by_id;              // Hash map para búsqueda O(1) por query_id
    pthread_rwlock_t* query_sessions_rwlock;  // Cambiado a rwlock para lecturas concurrentes
    t_list* workers_list;
    t_dictionary* workers_by_socket;          // Hash map para búsqueda O(1) por socket
    pthread_rwlock_t* workers_list_rwlock;    // Cambiado a rwlock para lecturas concurrentes
    pthread_cond_t* scheduler_cond;           // Condition variable para señalización
    pthread_mutex_t* scheduler_mutex;         // Mutex asociado a condition variable
    int tiempo_aging_ms;  // Tiempo de aging en milisegundos
} t_scheduler_args;

typedef struct {
    t_log* logger;
    t_list* query_sessions;
    pthread_rwlock_t* query_sessions_rwlock;
    t_list* workers_list;
    t_dictionary* workers_by_socket;
    pthread_rwlock_t* workers_list_rwlock;
    int check_interval_sec;  // Intervalo de chequeo en segundos
    int query_timeout_sec;   // Timeout para queries activas sin progreso
} t_watchdog_args;

t_config_master* cargar_configuracion(char* path_config);
void* handle_client_thread(void* arg);
void* scheduler_thread(void* arg);
void* scheduler_priority_thread(void* arg);
void* aging_thread(void* arg);
void* watchdog_thread(void* arg);

// Funciones de cleanup de recursos
void free_query_session(t_query_session* session);
void free_worker_info(t_worker_info* worker);

#endif /* MASTER_H */
