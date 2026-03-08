#include "instructions.h"
#include <utils/protocol.h>  

extern uint32_t current_query_id;

int execute_create(const char *instruccion, t_log *logger, int storage_fd)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    char *token = strtok(copia, " ");
    if (!token || strcmp(token, "CREATE") != 0)
        return -1;

    char *file_tag = strtok(NULL, " ");
    if (!file_tag)
    {
        log_error(logger, "[CREATE] [WORKER] Error de sintaxis — Falta parámetro <File:Tag>");
        return -1;
    }

    char *colon = strchr(file_tag, ':');
    if (!colon)
    {
        log_error(logger, "[CREATE] [WORKER] Error de sintaxis — Formato esperado <File:Tag>");
        return -1;
    }

    *colon = '\0';
    const char *file = file_tag;
    const char *tag = colon + 1;

    // Ejecución de CREATE
    if (protocol_send_worker_create(storage_fd, file, tag, 0, current_query_id) < 0)
    {
        log_error(logger, "[CREATE] Error al enviar — File=%s Tag=%s", file, tag);
        return -1;
    }

    // Esperar respuesta
    uint8_t resp;
    ssize_t recv_status = recv(storage_fd, &resp, 1, 0);

    if (recv_status <= 0)
    {
        log_error(logger, "[CREATE] No se recibió respuesta — File=%s Tag=%s", file, tag);
        return -1;
    }
    else if (resp != ST_OK)
    {
        log_error(logger, "\x1b[1;31m[CREATE] Storage reportó error 0x%02X (archivo ya existe) — File=%s Tag=%s\x1b[0m", resp, file, tag);
        return -1;  // Error propagado
    }
    
    log_trace(logger, "\x1b[1;32m[CREATE] Confirmación recibida (resp=0x%02X ST_OK) — File=%s Tag=%s\x1b[0m", resp, file, tag);
    return 0;  // Éxito
}
