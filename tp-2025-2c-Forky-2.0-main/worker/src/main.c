#include "./main.h"
#include <stdio.h>
#include <unistd.h>
#include <utils/protocol.h>
#include <utils/parameters.h>
#include "connections/connections.h"
#include "instrucctions/instructions.h"

#define MODULE_NAME "Worker"
#define MIN_ARGS 2
#define MAX_ARGS 2
uint32_t out_pc = 0;

// Variable global para el logger
t_log* logger;

int main(int argc, char *argv[])
{
    if (!valid_range_params(argc, MIN_ARGS, MAX_ARGS))
    {
        error_show("\x1b[31m[[Abortar!]]\x1b[0m No se respetó el formato: [archivo_config] [ID Worker].");
        abort();
    }

    char *archivo_config = argv[1];
    char *id_worker = argv[2];

    // === Cargar configuración ===
    t_config_worker *config = cargar_configuracion(archivo_config);
    if (config == NULL)
    {
        error_show("\x1b[31m[[Abortar!]]\x1b[0m La configuración fue NULA.");
        abort();
    }

    // === Logger ===
    logger = log_create("worker.log", MODULE_NAME, true, log_level_from_string(config->log_level));

    log_debug(logger, "[✓] Módulo inicializado OK. cfg=%s id=%s", archivo_config, id_worker);

    // === Conexión al Storage ===
    int storage_conn = connect_storage_server(logger, config);
    if (storage_conn < 0)
    {
        log_error(logger, "[x] No se pudo conectar al módulo Storage.");
        abort();
    }

    // Worker -> Storage: Handshake
    // Convierto el id de worker a entero sin signo y se lo envio a Storage
    uint32_t worker_id = (uint32_t)atoi(id_worker);
    if (protocol_send_worker_handshake(storage_conn, worker_id) < 0)
    {
        log_error(logger, "[x] Falló el envío del handshake al Storage. id=%s", id_worker);
        disconnect_storage_server(logger, storage_conn);
        abort();
    }

    // Storage -> Worker: Handshake OK o Error
    // Recibo el OK o error del Storage y si esta OK recibo el block_size
    uint32_t block_size = 0;
    int result = protocol_recv_storage_handshake_ok(storage_conn, &block_size);

    if (result < 0) {
        if (result == -2) {
            log_error(logger, "[x] Handshake fallido: Storage envió ST_ERROR.");
        } else {
            log_error(logger, "[x] Error de conexión durante el Handshake con Storage.");
        }

        disconnect_storage_server(logger, storage_conn);
        abort();
    }

    log_info(logger, "[✓] Handshake exitoso con Storage. id=%s block_size=%uB", id_worker, block_size);


    // === Inicializar Memoria Interna ===
    // IMPORTANTE: Se inicializa DESPUÉS del handshake para usar el block_size del Storage
    t_algoritmo_reemplazo algoritmo = parse_algoritmo_reemplazo(config->algoritmo_reemplazo);

    if (memoria_interna_init(config->tam_memoria, block_size, config->retardo_memoria, algoritmo, storage_conn, logger) < 0)
    {
        log_error(logger, "[x] Error al inicializar la memoria interna.");
        disconnect_storage_server(logger, storage_conn);
        abort();
    }

    uint32_t cantidad_marcos = config->tam_memoria / block_size;
    log_debug(logger, "[✓] Memoria interna inicializada. tam=%dB página=%uB marcos=%u alg=%s retardo=%dms",
             config->tam_memoria, block_size, cantidad_marcos, config->algoritmo_reemplazo, config->retardo_memoria);

    // === Conexión al Master ===
    int worker_id_int = atoi(id_worker); // Convertir string a int
    int master_conn = connect_master_server(logger, config, worker_id_int);
    if (master_conn < 0)
    {
        log_error(logger, "[x] No se pudo conectar al módulo Master.");
        disconnect_storage_server(logger, storage_conn);
        abort();
    }

    // Mostrar configuración de memoria y algoritmo de reemplazo
    log_info(logger, "\x1b[32m[✓] Worker iniciado correctamente.\x1b[0m");
    log_info(logger, "\x1b[32m    → Worker ID: %s\x1b[0m", id_worker);
    log_info(logger, "\x1b[32m    → Memoria: %u bytes (%u marcos de %u bytes)\x1b[0m", 
             config->tam_memoria, cantidad_marcos, block_size);
    log_info(logger, "\x1b[32m    → Algoritmo de Reemplazo: %s\x1b[0m", config->algoritmo_reemplazo);
    log_info(logger, "\x1b[32m    → Retardo de Memoria: %u ms\x1b[0m", config->retardo_memoria);

    // === Loop principal ===
    while (is_master_server_active(logger, master_conn))
    {
        uint32_t query_id;
        char *received_path = NULL;
        uint32_t pc;
        int result = protocol_recv_master_path(master_conn, &query_id, &received_path, &pc);

        if (result == 0)
        {
            log_info(logger, "\x1b[36m## Query <%u>: Se recibe la Query. El path de operaciones es: <%s> - PC inicial: <%u>\x1b[0m", query_id, received_path, pc);
            
            // Ejecutar la tarea pasando Storage y Master (usar PC recibido del Master, NO resetear a 0)
            uint32_t worker_id = (uint32_t)atoi(id_worker);
            int exec_result = ejecutar_tarea(query_id, received_path, &pc, storage_conn, master_conn, logger, config, worker_id);

            free(received_path);

            // Si la query fue desalojada (1), el Worker debe continuar esperando nuevas queries
            if (exec_result == 1) {
                log_info(logger, "\x1b[1;33m⚡ WORKER DISPONIBLE DESPUÉS DE DESALOJO - Esperando nuevas queries...\x1b[0m");
                continue; // Volver al inicio del loop, esperar nueva query
            }
            // Si hubo error (-1) o finalizó OK (0), continúa normalmente
        }
        else
        {
            log_warning(logger, "[!] Conexión con Master finalizada o error en recepción.");
            break;
        }
    }

    // === Desconexiones y cleanup ===
    disconnect_master_server(logger, master_conn);
    disconnect_storage_server(logger, storage_conn);

    memoria_interna_destroy();

    log_info(logger, "[✓] Worker finalizado correctamente.");

    return EXIT_SUCCESS;
}
