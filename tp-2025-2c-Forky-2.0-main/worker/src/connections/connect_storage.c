#include "connections.h"
#include <commons/string.h>
#include <errno.h>
#include <sys/socket.h>
#include "../main.h"

int connect_storage_server(t_log *logger, t_config_worker *config)
{
    if (!logger || !config)
    {
        log_error(logger, "[SOCKET] Logger o config nulos en connect_storage_server.");
        return -1;
    }

    char *puerto_str = string_from_format("%d", config->puerto_storage);
    int server = create_connection(logger, puerto_str, config->ip_storage);
    free(puerto_str);

    if (server < 0)
    {
        log_error(logger,
                  "[x] Error al conectar con Storage. host=%s puerto=%d",
                  config->ip_storage,
                  config->puerto_storage);
        return -1;
    }

    log_debug(logger,
             "[✓] Conectado exitosamente al Storage. <%s:%d>",
             config->ip_storage,
             config->puerto_storage);

    return server;
}

void disconnect_storage_server(t_log *logger, int storage_conn)
{
    if (storage_conn < 0)
    {
        log_warning(logger,
                    "[!] Conexión inválida o ya cerrada. storage_conn=%d",
                    storage_conn);
        return;
    }

    destroy_connection(logger, &storage_conn);

    log_warning(logger,
             "[SOCKET] Desconectado del Storage. storage_conn=%d",
             storage_conn);
}

int is_storage_server_active(t_log *logger, int storage_conn)
{
    (void)logger; // no se usa por ahora

    if (storage_conn < 0)
    {
        return 0;
    }

    // TODO: Implementar ping/heartbeat al Storage
    // Por ahora verificamos que el socket esté válido
    char test_byte;
    ssize_t result = recv(storage_conn, &test_byte, 1, MSG_PEEK | MSG_DONTWAIT);

    if (result == 0)
    {
        // Conexión cerrada por el otro extremo
        return 0;
    }
    else if (result < 0)
    {
        // Si el error es EAGAIN/EWOULDBLOCK, significa que no hay datos pero la conexión sigue viva
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 1;
        }
        // Otros errores => conexión caída
        return 0;
    }

    // Hay datos disponibles → conexión activa
    return 1;
}
