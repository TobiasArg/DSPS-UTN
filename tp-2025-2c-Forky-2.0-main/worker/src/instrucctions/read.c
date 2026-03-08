#include "instructions.h"
#include "../main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>


// =========================================================
// 📘 READ — Instrucción para lectura de datos
// =========================================================
// Reglas de log aplicadas:
// [NIVEL] [<INSTRUCCIÓN>] [<ORIGEN> → <DESTINO>] <Descripción> — <Resultado o estado>
// =========================================================

// Declaraciones de funciones auxiliares
static int solicitar_pagina_storage(int storage_fd, const char* file_tag, uint32_t numero_pagina, t_log* logger, char* error_msg_out, size_t error_msg_size, uint32_t query_id);
static int cargar_pagina_en_memoria(const char* file_tag, uint32_t numero_pagina, const void* data, uint32_t size, t_log* logger);

// Variable thread-local para almacenar el último mensaje de error de Storage
static __thread char last_storage_error[512] = {0};

int execute_read(const char *instruccion, t_log *logger, int storage_fd, int master_fd, uint32_t query_id)
{
    // ========================
    // 🧩 Parseo de instrucción
    // ========================
    char copia[256];
    strncpy(copia, instruccion, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    strtok(copia, " "); // Consumir "READ"
    char *file_tag = strtok(NULL, " ");
    char *dir_str  = strtok(NULL, " ");
    char *size_str = strtok(NULL, " ");

    if (!file_tag || !dir_str || !size_str) {
        log_error(logger, "[READ] [WORKER] Error de sintaxis — Formato esperado: READ <File:Tag> <DirBase> <Size>");
        return -1;
    }

    char *file = strtok(file_tag, ":");
    char *tag  = strtok(NULL, ":");
    if (!file || !tag) {
        log_error(logger, "[READ] [WORKER] Error de sintaxis — Formato de File:Tag inválido");
        return -1;
    }

    uint32_t dir_base = (uint32_t)atoi(dir_str);
    uint32_t size     = (uint32_t)atoi(size_str);

    log_info(logger, "[READ] [WORKER] Iniciando lectura — File=%s Tag=%s Dir=%u Size=%u", file, tag, dir_base, size);

    // ========================
    // 🧠 Lectura desde memoria
    // ========================
    void* buffer = malloc(size);
    if (!buffer) {
        log_error(logger, "[READ] [WORKER] Fallo al asignar memoria para buffer de lectura");
        return -1;
    }

    char file_tag_completo[256];
    snprintf(file_tag_completo, sizeof(file_tag_completo), "%s:%s", file, tag);

    uint32_t bytes_leidos = 0;
    uint32_t offset_actual = dir_base;
    uint32_t bytes_restantes = size;

    while (bytes_restantes > 0) {
        uint32_t numero_pagina = offset_actual / memoria_worker->tamanio_pagina;
        uint32_t offset_en_pagina = offset_actual % memoria_worker->tamanio_pagina;
        uint32_t bytes_en_esta_pagina = memoria_worker->tamanio_pagina - offset_en_pagina;

        if (bytes_en_esta_pagina > bytes_restantes)
            bytes_en_esta_pagina = bytes_restantes;

        int resultado_lectura = memoria_leer_pagina(
            file_tag_completo,
            numero_pagina,
            offset_en_pagina,
            (char*)buffer + bytes_leidos,
            bytes_en_esta_pagina
        );

        if (resultado_lectura != 0) {
            log_info(logger, "[READ] [WORKER → STORAGE] Página %u no disponible en memoria — Solicitando a Storage", numero_pagina);

            // Limpiar mensaje de error anterior
            last_storage_error[0] = '\0';
            
            if (solicitar_pagina_storage(storage_fd, file_tag_completo, numero_pagina, logger, last_storage_error, sizeof(last_storage_error), query_id) != 0) {
                log_error(logger, "\x1b[1;31m[READ] [WORKER → STORAGE] Error al solicitar página %u a Storage\x1b[0m", numero_pagina);
                log_error(logger, "\x1b[1;31m[READ] Error guardado: %s\x1b[0m", last_storage_error[0] != '\0' ? last_storage_error : "(sin mensaje)");
                free(buffer);
                return -1;  // El mensaje de error está en last_storage_error
            }

            resultado_lectura = memoria_leer_pagina(
                file_tag_completo,
                numero_pagina,
                offset_en_pagina,
                (char*)buffer + bytes_leidos,
                bytes_en_esta_pagina
            );

            if (resultado_lectura != 0) {
                // Intentar obtener el error específico de Storage
                const char* error_memoria = memoria_get_ultimo_error_storage();
                if (error_memoria) {
                    snprintf(last_storage_error, sizeof(last_storage_error), "%s", error_memoria);
                    log_error(logger, "[READ] [WORKER] %s", error_memoria);
                } else {
                    snprintf(last_storage_error, sizeof(last_storage_error), 
                             "ERROR: Fallo al leer página %u desde Storage", numero_pagina);
                    log_error(logger, "[READ] [WORKER] Fallo al leer página %u incluso tras cargarla desde Storage", numero_pagina);
                }
                free(buffer);
                return -1;
            }
        }

        bytes_leidos += bytes_en_esta_pagina;
        offset_actual += bytes_en_esta_pagina;
        bytes_restantes -= bytes_en_esta_pagina;
    }

    // ========================
    // 📤 Envío al Master
    // ========================
    // query_id viene como parámetro desde instructions.c
    
    if (protocol_send_worker_read_result(master_fd, query_id, file_tag_completo, buffer, size) < 0) {
        log_error(logger, "[READ] [WORKER → MASTER] Error al enviar resultado de lectura al Master");
        free(buffer);
        return -1;
    }

    log_info(logger, "[READ] [WORKER → MASTER] Resultado enviado correctamente — %u bytes", size);
    log_info(logger, "[READ] [WORKER] Lectura completada — File:Tag=%s Dir=%u Size=%u", file_tag_completo, dir_base, size);
    log_debug(logger, "[READ] [WORKER] Datos leídos: %.50s%s", (char*)buffer, size > 50 ? "..." : "");

    free(buffer);
    return 0;
}

// Función para obtener el último error de Storage
const char* get_last_storage_error(void) {
    return last_storage_error[0] != '\0' ? last_storage_error : NULL;
}

// =========================================================
// 📦 Solicitar página a STORAGE
// =========================================================
static int solicitar_pagina_storage(int storage_fd, const char* file_tag, uint32_t numero_pagina, t_log* logger, char* error_msg_out, size_t error_msg_size, uint32_t query_id)
{
    // Limpiar mensaje de error
    if (error_msg_out && error_msg_size > 0) {
        error_msg_out[0] = '\0';
    }
    
    log_info(logger, "[READ] [WORKER → STORAGE] Solicitando página %u de %s para query %u", numero_pagina, file_tag, query_id);

    // Separar file:tag
    char file_tag_copia[256];
    strncpy(file_tag_copia, file_tag, sizeof(file_tag_copia) - 1);
    file_tag_copia[sizeof(file_tag_copia) - 1] = '\0';
    
    char *file = strtok(file_tag_copia, ":");
    char *tag = strtok(NULL, ":");
    
    if (!file || !tag) {
        log_error(logger, "[READ] [WORKER] Error parseando File:Tag: %s", file_tag);
        return -1;
    }

    // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num]
    uint8_t op = OP_STORAGE_READ;
    uint32_t query_id_net = htonl(query_id);
    uint32_t file_len = strlen(file);
    uint32_t tag_len = strlen(tag);
    uint32_t file_len_net = htonl(file_len);
    uint32_t tag_len_net = htonl(tag_len);
    uint32_t block_num_net = htonl(numero_pagina);

    if (send(storage_fd, &op, 1, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[READ] [WORKER → STORAGE] Error enviando operación");
        return -1;
    }

    // Enviar query_id
    if (send(storage_fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[READ] [WORKER → STORAGE] Error enviando query_id");
        return -1;
    }

    // Enviar file
    if (send(storage_fd, &file_len_net, 4, MSG_NOSIGNAL) <= 0 ||
        send(storage_fd, file, file_len, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[READ] [WORKER → STORAGE] Error enviando file");
        return -1;
    }

    // Enviar tag
    if (send(storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) <= 0 ||
        send(storage_fd, tag, tag_len, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[READ] [WORKER → STORAGE] Error enviando tag");
        return -1;
    }

    // Enviar número de página/bloque
    if (send(storage_fd, &block_num_net, 4, MSG_NOSIGNAL) <= 0) {
        log_error(logger, "[READ] [WORKER → STORAGE] Error enviando número de página");
        return -1;
    }

    // Recibir respuesta
    uint8_t response;
    if (recv(storage_fd, &response, 1, MSG_WAITALL) != 1) {
        log_error(logger, "[READ] [STORAGE → WORKER] Error al recibir código de respuesta");
        return -1;
    }

    if (response != ST_OK) {
        // Storage reportó error (bloque fuera de límite, archivo no existe, etc.)
        log_error(logger, "[READ] [STORAGE → WORKER] Storage reportó ST_ERROR (0x%02X) al leer %s:%s bloque %u", 
                 response, file, tag, numero_pagina);
        
        // Guardar mensaje de error descriptivo
        if (error_msg_out && error_msg_size > 0) {
            snprintf(error_msg_out, error_msg_size,
                     "ERROR: Storage reportó bloque %u fuera de límite o no disponible para %s:%s",
                     numero_pagina, file, tag);
        }
        return -1;
    }

    uint32_t data_size_net;
    if (recv(storage_fd, &data_size_net, 4, MSG_WAITALL) != 4) {
        log_error(logger, "[READ] [STORAGE → WORKER] Error al recibir tamaño de datos");
        return -1;
    }

    uint32_t data_size = ntohl(data_size_net);
    // Storage ahora siempre envía block_size (128 bytes)
    
    void* page_data = malloc(data_size);
    if (!page_data) {
        log_error(logger, "[READ] [WORKER] Fallo al asignar memoria para página recibida");
        return -1;
    }

    if (recv(storage_fd, page_data, data_size, MSG_WAITALL) != (ssize_t)data_size) {
        log_error(logger, "[READ] [STORAGE → WORKER] Error al recibir datos de página");
        free(page_data);
        return -1;
    }

    int resultado_carga = cargar_pagina_en_memoria(file_tag, numero_pagina, page_data, data_size, logger);
    free(page_data);

    if (resultado_carga != 0) {
        log_error(logger, "[READ] [WORKER] Fallo al cargar página %u en memoria", numero_pagina);
        return -1;
    }

    log_info(logger, "[READ] [STORAGE → WORKER] Página %u cargada correctamente en memoria", numero_pagina);
    return 0;
}

// =========================================================
// 💾 Cargar página en memoria
// =========================================================
static int cargar_pagina_en_memoria(const char* file_tag, uint32_t numero_pagina, const void* data, uint32_t size, t_log* logger)
{
    t_tabla_paginas* tabla = memoria_buscar_tabla_paginas(file_tag);

    if (!tabla) {
        uint32_t total_paginas = numero_pagina + 1;
        tabla = memoria_crear_tabla_paginas(file_tag, total_paginas);
        if (!tabla) {
            log_error(logger, "[READ] [WORKER] Error creando tabla de páginas para %s", file_tag);
            return -1;
        }
    }

    t_pagina* pagina = NULL;
    for (int i = 0; i < list_size(tabla->paginas); i++) {
        t_pagina* p = list_get(tabla->paginas, i);
        if (p->numero_pagina == numero_pagina) {
            pagina = p;
            break;
        }
    }

    if (!pagina) {
        pagina = memoria_crear_pagina(numero_pagina, file_tag);
        if (!pagina) {
            log_error(logger, "[READ] [WORKER] Error creando página %u", numero_pagina);
            return -1;
        }
        list_add(tabla->paginas, pagina);
    }

    if (memoria_cargar_pagina(pagina) != 0) {
        log_error(logger, "[READ] [WORKER] Error asignando marco físico a página %u", numero_pagina);
        return -1;
    }

    void* marco_direccion = (char*)memoria_worker->memoria_base + (pagina->marco * memoria_worker->tamanio_pagina);
    memcpy(marco_direccion, data, size);

    if (size < memoria_worker->tamanio_pagina)
        memset((char*)marco_direccion + size, 0, memoria_worker->tamanio_pagina - size);

    log_info(logger, "[READ] [WORKER] Página %u cargada en marco físico %u", numero_pagina, pagina->marco);
    return 0;
}
