#include "instructions.h"
#include "../main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static int enviar_pagina_storage(int storage_fd, const char* file_tag, uint32_t numero_pagina, const void* data, t_log* logger, uint32_t query_id);

int execute_write(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id)
{
    // Parseo de instrucción
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    // Formato esperado: WRITE file:tag dir_base contenido
    strtok(copia, " "); // Consumir "WRITE"
    char *file_tag = strtok(NULL, " ");
    char *dir_str = strtok(NULL, " ");
    char *contenido = strtok(NULL, ""); // El resto de la línea

    if (!file_tag || !dir_str || !contenido)
    {
        log_error(logger, "[WRITE] Error de sintaxis — Formato esperado: WRITE <file:tag> <dir_base> <contenido>");
        return -1;
    }

    // Parseo de parámetros
    char file_tag_original[256];
    strncpy(file_tag_original, file_tag, sizeof(file_tag_original) - 1);
    file_tag_original[sizeof(file_tag_original) - 1] = '\0';

    char *colon = strchr(file_tag, ':');
    if (!colon)
    {
        log_error(logger, "[WRITE] Error de sintaxis — Faltante ':' en parámetro '%s'", file_tag);
        return -1;
    }

    *colon = '\0';
    const char *file = file_tag;
    const char *tag = colon + 1;

    uint32_t dir_base = (uint32_t)atoi(dir_str);
    uint32_t len = strlen(contenido);

    log_info(logger, "[WRITE] Iniciando escritura — File=%s Tag=%s DirBase=%u Tamaño=%u", file, tag, dir_base, len);

    // Escritura en memoria interna
    uint32_t pagina_inicial = dir_base / memoria_worker->tamanio_pagina;
    uint32_t offset_inicial = dir_base % memoria_worker->tamanio_pagina;

    log_info(logger, "[WRITE] Resolviendo página=%u offset=%u", pagina_inicial, offset_inicial);

    // Manejar escrituras que cruzan límites de página
    uint32_t bytes_escritos = 0;
    uint32_t pagina_actual = pagina_inicial;
    uint32_t offset_actual = offset_inicial;

    while (bytes_escritos < len) {
        // Calcular cuántos bytes escribir en la página actual
        uint32_t espacio_disponible = memoria_worker->tamanio_pagina - offset_actual;
        uint32_t bytes_a_escribir = (len - bytes_escritos) < espacio_disponible ? 
                                    (len - bytes_escritos) : espacio_disponible;

        log_debug(logger, "[WRITE] Escribiendo %u bytes en página %u offset %u", 
                  bytes_a_escribir, pagina_actual, offset_actual);

        // Escribir en la página actual
        int resultado = memoria_escribir_pagina(file_tag_original, pagina_actual, offset_actual, 
                                               contenido + bytes_escritos, bytes_a_escribir);

        if (resultado < 0) {
            log_error(logger, "[WRITE] Falló escritura en página %u — File=%s Tag=%s", 
                      pagina_actual, file, tag);
            return -1;
        }

        // Persistir página modificada al Storage
        void* datos_pagina = malloc(memoria_worker->tamanio_pagina);
        if (!datos_pagina) {
            log_error(logger, "[WRITE] Error asignando memoria para enviar página %u", pagina_actual);
            return -1;
        }

        // Leer la página completa desde memoria del Worker
        if (memoria_leer_pagina(file_tag_original, pagina_actual, 0, datos_pagina, memoria_worker->tamanio_pagina) != 0) {
            log_error(logger, "[WRITE] Error leyendo página %u para envío", pagina_actual);
            free(datos_pagina);
            return -1;
        }

        // Enviar página completa al Storage
        if (enviar_pagina_storage(storage_fd, file_tag_original, pagina_actual, datos_pagina, logger, query_id) != 0) {
            log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando página %u al Storage", pagina_actual);
            free(datos_pagina);
            return -1;
        }

        free(datos_pagina);

        // Avanzar a la siguiente página
        bytes_escritos += bytes_a_escribir;
        pagina_actual++;
        offset_actual = 0;  // Las páginas siguientes siempre empiezan desde offset 0
    }

    log_info(logger, "[WRITE] Escritura completada correctamente — %u bytes escritos en %u página(s)", 
             len, (pagina_actual - pagina_inicial));
    return 0;  // Éxito
}

static int enviar_pagina_storage(int storage_fd, const char* file_tag, uint32_t numero_pagina, const void* data, t_log* logger, uint32_t query_id)
{
    log_info(logger, "[WRITE] [WORKER → STORAGE] Enviando página %u de %s para query %u", numero_pagina, file_tag, query_id);

    // Separar file:tag
    char file_tag_copia[256];
    strncpy(file_tag_copia, file_tag, sizeof(file_tag_copia) - 1);
    file_tag_copia[sizeof(file_tag_copia) - 1] = '\0';
    
    char *file = strtok(file_tag_copia, ":");
    char *tag = strtok(NULL, ":");
    
    if (!file || !tag) {
        log_error(logger, "[WRITE] Error parseando File:Tag: %s", file_tag);
        return -1;
    }

    // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num][block_data]
    uint8_t op = OP_STORAGE_WRITE;
    uint32_t query_id_net = htonl(query_id);
    uint32_t file_len = strlen(file);
    uint32_t tag_len = strlen(tag);
    uint32_t file_len_net = htonl(file_len);
    uint32_t tag_len_net = htonl(tag_len);
    uint32_t block_num_net = htonl(numero_pagina);

    if (send(storage_fd, &op, 1, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando operación");
        return -1;
    }

    // Enviar query_id
    if (send(storage_fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando query_id");
        return -1;
    }

    // Enviar file
    if (send(storage_fd, &file_len_net, 4, MSG_NOSIGNAL) <= 0 ||
        send(storage_fd, file, file_len, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando file");
        return -1;
    }

    // Enviar tag
    if (send(storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) <= 0 ||
        send(storage_fd, tag, tag_len, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando tag");
        return -1;
    }

    // Enviar número de página/bloque
    if (send(storage_fd, &block_num_net, 4, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando número de página");
        return -1;
    }

    // Enviar datos completos de la página (siempre block_size bytes)
    uint32_t block_size = memoria_worker->tamanio_pagina; // Debería ser 128 bytes
    if (send(storage_fd, data, block_size, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[WRITE] [WORKER → STORAGE] Error enviando datos de página");
        return -1;
    }

    // Recibir respuesta
    uint8_t response;
    if (recv(storage_fd, &response, 1, MSG_WAITALL) != 1) {
        log_error(logger, "[WRITE] [STORAGE → WORKER] Error al recibir respuesta");
        return -1;
    }

    if (response != ST_OK) {
        log_error(logger, "[WRITE] [STORAGE → WORKER] Storage reportó error al escribir página");
        return -1;
    }

    log_info(logger, "[WRITE] [STORAGE → WORKER] Página %u persistida correctamente", numero_pagina);
    return 0;
}