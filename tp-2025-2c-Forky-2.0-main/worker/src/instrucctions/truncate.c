#include "instructions.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

int execute_truncate(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    // Formato esperado: TRUNCATE file:tag tamaño
    strtok(copia, " "); // Consumir "TRUNCATE"
    char *file_tag = strtok(NULL, " ");
    char *size_str = strtok(NULL, " ");

    if (!file_tag || !size_str)
    {
        log_error(logger, "[TRUNCATE] [WORKER] Error de sintaxis — Formato esperado: TRUNCATE <archivo:tag> <tamaño>");
        return -1;
    }

    // Separar file:tag
    char *colon = strchr(file_tag, ':');
    if (!colon)
    {
        log_error(logger, "[TRUNCATE] [WORKER] Error de sintaxis — Faltante ':' en parámetro '%s'", file_tag);
        return -1;
    }

    *colon = '\0';
    const char *file = file_tag;
    const char *tag = colon + 1;
    uint32_t new_size = (uint32_t)atoi(size_str);

    // Envío al Storage
    if (protocol_send_worker_truncate(storage_fd, file, tag, new_size, query_id) < 0) 
    {
        log_error(logger, "[TRUNCATE] Error al enviar (file='%s', tag='%s')", file, tag);
        return -1;
    }

    // Recepción de respuesta
    uint8_t resp;
    ssize_t recv_status = recv(storage_fd, &resp, 1, 0);

    if (recv_status <= 0)
    {
        log_error(logger, "[TRUNCATE] No se recibió respuesta (file='%s', tag='%s')", file, tag);
        return -1;
    }

    if (resp != ST_OK)
    {
        log_error(logger, "[TRUNCATE] Error — Código: %u (file='%s', tag='%s')", resp, file, tag);
    }
    return 0;
}
