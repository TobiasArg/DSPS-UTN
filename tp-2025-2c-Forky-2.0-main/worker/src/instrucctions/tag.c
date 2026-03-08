#include "instructions.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>

int execute_tag(const char *instruccion, t_log *logger, int storage_fd)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    strtok(copia, " "); // Consumir "TAG"
    char *origen_full  = strtok(NULL, " ");
    char *destino_full = strtok(NULL, " ");

    if (!origen_full || !destino_full) {
        log_error(logger, "[TAG] [WORKER] Error de sintaxis — Formato esperado: TAG <Archivo:Tag> <Archivo:Tag>");
        return -1;
    }

    char origen_copy[256], destino_copy[256];
    strncpy(origen_copy, origen_full, sizeof(origen_copy) - 1);
    strncpy(destino_copy, destino_full, sizeof(destino_copy) - 1);
    origen_copy[sizeof(origen_copy) - 1] = '\0';
    destino_copy[sizeof(destino_copy) - 1] = '\0';

    char *file_orig = strtok(origen_copy, ":");
    char *tag_orig  = strtok(NULL, ":");
    char *file_dst  = strtok(destino_copy, ":");
    char *tag_dst   = strtok(NULL, ":");

    if (!file_orig || !tag_orig || !file_dst || !tag_dst) {
        log_error(logger, "[TAG] [WORKER] Error de sintaxis — Parámetros inválidos (faltan ':' en alguno)");
        return -1;
    }

    uint32_t query_id = 0;

    // Envío de solicitud al Storage
    if (protocol_send_worker_tag(storage_fd, file_orig, tag_orig, file_dst, tag_dst, query_id) < 0) {
        log_error(logger, "[TAG] Error al enviar: %s", strerror(errno));
        return -1;
    }

    // Recepción de respuesta
    int response_code = protocol_recv_storage_response(storage_fd);

    if (response_code < 0) {
        log_error(logger, "[TAG] Error al recibir respuesta");
        return -1;
    }

    if (response_code != ST_OK) {
        log_error(logger, "[TAG] Error en operación — Código: %d (tag ya existe)", response_code);
        return -1;  // Error propagado
    }
    return 0;  // Éxito
}
