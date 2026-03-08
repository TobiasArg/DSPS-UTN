#include "instructions.h"
#include <string.h>
#include <stdio.h>

int execute_commit(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    strtok(copia, " "); // Consumir "COMMIT"
    char *file_tag = strtok(NULL, " ");
    if (!file_tag) {
        log_error(logger, "[COMMIT] [WORKER] Error de sintaxis — Falta parámetro <File:Tag>");
        return -1;
    }

    // Guardar copia del file_tag antes de modificarlo con strtok
    char file_tag_original[256];
    strncpy(file_tag_original, file_tag, sizeof(file_tag_original) - 1);
    file_tag_original[sizeof(file_tag_original) - 1] = '\0';

    char *file = strtok(file_tag, ":");
    char *tag  = strtok(NULL, ":");
    if (!file || !tag) {
        log_error(logger, "[COMMIT] [WORKER] Error de sintaxis — Formato esperado <File:Tag>");
        return -1;
    }

    // Ejecución
    // Ejecutar flush implícito previo al commit
    char flush_instruction[512];
    snprintf(flush_instruction, sizeof(flush_instruction), "FLUSH %s", file_tag_original);
    execute_flush(flush_instruction, logger, storage_fd, query_id);

    // Enviar al Storage
    if (protocol_send_worker_commit(storage_fd, file, tag, query_id) < 0) {
        log_error(logger, "[COMMIT] Error al enviar — File=%s Tag=%s", file, tag);
        return -1;
    }

    // Esperar respuesta
    int response_code = protocol_recv_storage_response(storage_fd);
    if (response_code != ST_OK) {
        log_error(logger, "[COMMIT] Error en commit — File=%s Tag=%s", file, tag);
    }
    return 0;
}
