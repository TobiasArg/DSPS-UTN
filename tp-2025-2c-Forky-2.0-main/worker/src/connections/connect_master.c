#include "connections.h"
#include <commons/string.h>
#include "../main.h"
#include <utils/protocol.h>

int connect_master_server(t_log *logger, t_config_worker *config, int worker_id)
{
    if (!logger || !config)
    {
        log_error(logger, "[x] Logger o config nulos en connect_master_server.");
        return -1;
    }

    // Crear conexión TCP con el Master
    char *puerto_str = string_from_format("%d", config->puerto_master);
    int server = create_connection(logger, puerto_str, config->ip_master);
    free(puerto_str);

    if (server < 0)
    {
        log_error(logger,
                  "[x] Error al conectar con Master. host=%s puerto=%d",
                  config->ip_master,
                  config->puerto_master);
        return -1;
    }

    log_debug(logger,
             "[✓] Conectado exitosamente al Master. <%s:%d>",
             config->ip_master,
             config->puerto_master);

    log_trace(logger,
             "Enviando registro al Master... worker_id=%d",
             worker_id);

    // Enviar registro
    if (protocol_send_worker_register_master(server, worker_id) == 0)
    {
        uint32_t ack_id = 0;
        if (protocol_recv_worker_register_ack(server, &ack_id) == 0)
        {
            log_info(logger, "[✓] Handshake exitoso con Master. worker_id=%u", ack_id);
        }
        else
        {
            log_error(logger,
                      "[x] No se recibió ACK del Master tras el registro ❌ worker_id=%d",
                      worker_id);
        }
    }
    else
    {
        log_error(logger,
                  "[x] Error al enviar registro del Worker al Master ❌ worker_id=%d",
                  worker_id);
        close(server);
        return -1;
    }

    return server;
}

void disconnect_master_server(t_log *logger, int master_conn)
{
    if (master_conn < 0)
    {
        log_warning(logger,
                    "[!] Conexión inválida o ya cerrada. master_conn=%d",
                    master_conn);
        return;
    }

    destroy_connection(logger, &master_conn);

    log_warning(logger,
             "[!] Desconectado del Master. master_conn=%d",
             master_conn);
}

int is_master_server_active(t_log *logger, int master_conn)
{
    (void)logger;

    if (master_conn < 0)
        return 0;

    // TODO: implementar verificación real del estado del socket
    return 1;
}
