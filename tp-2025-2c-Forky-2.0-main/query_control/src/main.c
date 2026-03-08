#include "./main.h"
#include "./connections/connections.h"
#include <utils/parameters.h>
#include "utils/protocol.h"

#define MODULE_NAME "Query control"
#define MIN_ARGS 3
#define MAX_ARGS 3

int main(int argc, char *argv[])
{
    /*
    Valido el rando de los parametros, luego enviados al master.
    @param archivo configuración
    @param archivo de instrucciones
    @param prioridad del proceso
    */
    if (!valid_range_params(argc, MIN_ARGS, MAX_ARGS))
    {
        error_show("[[Abortar!]] No se respeto el formato: [archivo_config] [archivo_query] [prioridad].");
        abort();
    }

    char *archivo_config = argv[1];
    char *archivo_query = argv[2];
    int prioridad = atoi(argv[3]);

    /*
    Cargar las configuraciones del archivo de configuración
    *config es un objeto con los parametros del mismo
    */
    t_config_query_control *config = cargar_configuracion(archivo_config);
    if (config == NULL)
    {
        error_show("[[Abortar!]] La configuración fue NULA.");
        abort();
    }

    /*
    Registro el logger y la metadata
    Logger_** es un wrapper de log_
    metadata es un tipo de dupla para registrar información útil
    */
    t_log *logger = log_create("query.log", "query", true /* show in console */, log_level_from_string(config->log_level));

    /*
    Me intento conectar al modulo de master y espero su respuesta
    */
    log_debug(logger, "[✓] Inicialización del módulo completada.");

    /*
    Me intento conectar al master por medio del puerto de la configuración, si este no existe, cierro el programa
    TODO: Liberar la memoria actual (config, logger, metadata)
    */
    int master_conn = connect_master_server(logger, config);
    if (master_conn < 0)
        return EXIT_FAILURE;

    /*
    Una vez establecida la conexión leo el archivo de instrucciones enviadas por paramatro,
    si este no existe aborto y cierro la conexión
    TODO: TODO: Liberar la memoria actual (config, logger, metadata, conexión)
    */
    char *query_content = read_query_file_content(archivo_query);
    if (!query_content)
    {
        log_error(logger, "[[Abortar!]] No se pudo leer el archivo de query.");
        disconnect_master_server(logger, master_conn);
        return EXIT_FAILURE;
    }

    /*
    Limpiamos las instrucciones para que este sea enviado al master de forma estandar
    */
    char content_clean[4096];
    parse_query_content(content_clean, sizeof(content_clean), query_content);

    /*
    Enviamos la información del query al master por medio del protocolo estandar
    */
    if (protocol_send_query_submit(master_conn, archivo_query, prioridad) < 0)
    {
        error_show("[[Abortar!]] Falló el envío de la query al Master.");
        disconnect_master_server(logger, master_conn);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    log_info(logger, "## Solicitud de ejecución de Query: %s, prioridad: %d", archivo_query, prioridad);

    /*
    Si se reció la información del master confirmamos el envio de la instrucción
    Importante: Esto se hace antes del while debido a que se hace una vez...
    */
    uint32_t query_id = 0;
    if (protocol_recv_query_confirm(master_conn, &query_id) < 0)
    {
        error_show("[[Abortar!]] No se recibió confirmación del Master.");
        disconnect_master_server(logger, master_conn);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    log_debug(logger, "[✓] Master confirmó la recepción de la query. id=%d", query_id);
    // Espero respuestas del modulo master

    char *motivo_final = NULL;
    listen_to_master(logger, master_conn, &motivo_final);
    
    // Si el motivo contiene errores de Storage o empieza con "ERROR", mostrarlo en ROJO para visibilidad
    if (motivo_final && (strstr(motivo_final, "ERROR") != NULL || 
                         strstr(motivo_final, "LECTURA_FUERA_DEL_LIMITE") != NULL ||
                         strstr(motivo_final, "ESCRITURA_ARCHIVO_COMMITED") != NULL ||
                         strstr(motivo_final, "FILE_EXISTENTE") != NULL ||
                         strstr(motivo_final, "TAG_EXISTENTE") != NULL)) {
        log_info(logger, "\x1b[1;31m## Query Finalizada - %s\x1b[0m", motivo_final);
    } else {
        log_info(logger, "## Query Finalizada - %s",
                 motivo_final ? motivo_final : "Comunicación finalizada sin motivo específico");
    }

    if (motivo_final != NULL)
    {
        free(motivo_final);
    }

    // Me desconecto del modulo master
    disconnect_master_server(logger, master_conn);
    log_warning(logger, "[!] Query finalizada. Desconexión del módulo Master.");
    log_destroy(logger);
    return EXIT_SUCCESS;
}

