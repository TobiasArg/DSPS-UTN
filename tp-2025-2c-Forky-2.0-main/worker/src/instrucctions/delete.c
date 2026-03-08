#include "instructions.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int execute_delete(const char *instruccion, t_log *logger, int storage_fd)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    strtok(copia, " "); // Consumir "DELETE"
    char *file_tag = strtok(NULL, " ");
    if (!file_tag) {
        log_error(logger, "[DELETE] [WORKER] Error de sintaxis — Falta parámetro <File:Tag>");
        return -1;
    }

    // Separar File y Tag
    char *colon = strchr(file_tag, ':');
    if (!colon) {
        log_error(logger, "[DELETE] [WORKER] Error de sintaxis — Formato esperado <File:Tag>");
        return -1;
    }

    *colon = '\0';
    const char *file = file_tag;
    const char *tag = colon + 1;

    // Reconstruir file_tag porque Storage espera "FILE:TAG" como un solo string
    char file_tag_completo[256];
    snprintf(file_tag_completo, sizeof(file_tag_completo), "%s:%s", file, tag);
    
    log_info(logger, "[DELETE] Eliminando — File:Tag=%s", file_tag_completo);

    // Ejecución de DELETE - Storage espera solo el file_tag completo (FILE:TAG)
    uint8_t op = OP_STORAGE_DELETE;
    uint32_t file_tag_len = strlen(file_tag_completo);
    uint32_t file_tag_len_net = htonl(file_tag_len);

    if (send(storage_fd, &op, 1, 0) <= 0 ||
        send(storage_fd, &file_tag_len_net, 4, 0) <= 0 ||
        send(storage_fd, file_tag_completo, file_tag_len, 0) <= 0) {
        log_error(logger, "[DELETE] Error al enviar solicitud — File:Tag=%s", file_tag_completo);
        return -1;
    }

    // Esperar respuesta
    int response_code = protocol_recv_storage_response(storage_fd);
    if (response_code != ST_OK) {
        log_error(logger, "[DELETE] Error al eliminar — File:Tag=%s", file_tag_completo);
    } else {
        log_info(logger, "[DELETE] Eliminado exitosamente — File:Tag=%s", file_tag_completo);
    }
    return 0;
}
