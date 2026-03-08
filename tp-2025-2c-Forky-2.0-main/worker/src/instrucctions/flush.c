#include "instructions.h"
#include <string.h>
#include <stdio.h>
#include <commons/collections/list.h>
#include <arpa/inet.h>
#include <sys/socket.h>

extern t_memoria_interna *memoria_worker;
extern uint32_t current_query_id;

int execute_flush(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id)
{
    // Formato: FLUSH ARCHIVO:TAG
    char file_tag[256];
    
    if (sscanf(instruccion, "FLUSH %255s", file_tag) != 1) {
        log_error(logger, "[FLUSH] Formato inválido de instrucción: %s", instruccion);
        return -1;
    }
    
    log_info(logger, "[FLUSH] Persistiendo modificaciones de %s en Storage...", file_tag);
    
    // Obtener lista de páginas modificadas para este File:Tag
    t_list* paginas_modificadas = memoria_get_paginas_modificadas(file_tag);
    
    if (list_is_empty(paginas_modificadas)) {
        log_info(logger, "[FLUSH] No hay páginas modificadas para %s - Todo sincronizado", file_tag);
        list_destroy(paginas_modificadas);
        return 0;  // No es error, simplemente no había nada que persistir
    }
    
    int paginas_escritas = 0;
    int paginas_fallidas = 0;
    
    // Iterar sobre todas las páginas modificadas y escribirlas a Storage
    for (int i = 0; i < list_size(paginas_modificadas); i++) {
        t_pagina* pagina = list_get(paginas_modificadas, i);
        
        if (!pagina->presente) {
            // La página no está en memoria, saltar
            continue;
        }
        
        // Obtener datos de la página desde memoria
        void* datos_pagina = memoria_get_datos_pagina(pagina->marco);
        if (!datos_pagina) {
            log_warning(logger, "[FLUSH] No se pudieron obtener datos de página %u", pagina->numero_pagina);
            paginas_fallidas++;
            continue;
        }
        
        // NUEVO PROTOCOLO PAGINACIÓN: [OP][file_len][file][tag_len][tag][block_num][block_data]
        // Separar file:tag
        char file_tag_copy[256];
        strncpy(file_tag_copy, file_tag, sizeof(file_tag_copy) - 1);
        file_tag_copy[sizeof(file_tag_copy) - 1] = '\0';
        
        char *file = strtok(file_tag_copy, ":");
        char *tag = strtok(NULL, ":");
        
        if (!file || !tag) {
            log_error(logger, "[FLUSH] Error parseando file:tag '%s'", file_tag);
            paginas_fallidas++;
            continue;
        }
        
        uint8_t op = OP_STORAGE_WRITE;
        uint32_t query_id_net = htonl(query_id);
        uint32_t file_len = strlen(file);
        uint32_t tag_len = strlen(tag);
        uint32_t file_len_net = htonl(file_len);
        uint32_t tag_len_net = htonl(tag_len);
        uint32_t block_num_net = htonl(pagina->numero_pagina);
        
        // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num][block_data]
        // Enviar solicitud de escritura a Storage
        if (send(storage_fd, &op, 1, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, &query_id_net, 4, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, &file_len_net, 4, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, file, file_len, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, tag, tag_len, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, &block_num_net, 4, MSG_NOSIGNAL) > 0 &&
            send(storage_fd, datos_pagina, memoria_worker->tamanio_pagina, MSG_NOSIGNAL) > 0) {
            
            // Esperar confirmación del Storage
            uint8_t response;
            if (recv(storage_fd, &response, 1, MSG_WAITALL) == 1) {
                if (response == ST_OK) {
                    // Marcar página como no modificada después de escribirla
                    pagina->modificada = false;
                    paginas_escritas++;
                    log_debug(logger, "[FLUSH] Página %s/%u escrita exitosamente a Storage", 
                              file_tag, pagina->numero_pagina);
                } else {
                    log_warning(logger, "[FLUSH] Storage reportó error al escribir página %s/%u", 
                                file_tag, pagina->numero_pagina);
                    paginas_fallidas++;
                }
            } else {
                log_warning(logger, "[FLUSH] Error al recibir confirmación de Storage para página %s/%u", 
                            file_tag, pagina->numero_pagina);
                paginas_fallidas++;
            }
        } else {
            log_warning(logger, "[FLUSH] Error al enviar página %s/%u a Storage", 
                        file_tag, pagina->numero_pagina);
            paginas_fallidas++;
        }
    }
    
    list_destroy(paginas_modificadas);
    
    if (paginas_fallidas > 0) {
        log_warning(logger, "[FLUSH] %s: %d páginas escritas, %d fallidas", 
                    file_tag, paginas_escritas, paginas_fallidas);
        return -1;
    } else {
        log_info(logger, "[FLUSH] %s: %d páginas persistidas exitosamente en Storage", 
                 file_tag, paginas_escritas);
        
        // Log en turquesa del estado de todos los marcos después del FLUSH
        log_info(logger, "\x1b[36m## Query <%u>: Estado de Memoria después de FLUSH:\x1b[0m", query_id);
        for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
            t_pagina* pagina_marco = memoria_worker->marco_a_pagina[i];
            if (pagina_marco && pagina_marco->presente) {
                log_info(logger, "\x1b[36m   Marco %u: Página %u - Bit de Uso: %d - Bit Modificado: %d\x1b[0m",
                         i, pagina_marco->numero_pagina, 
                         pagina_marco->uso ? 1 : 0, 
                         pagina_marco->modificada ? 1 : 0);
            } else {
                log_info(logger, "\x1b[36m   Marco %u: [LIBRE]\x1b[0m", i);
            }
        }
        
        return 0;
    }
}

int flush_all_modified_pages(t_log *logger, int storage_fd)
{
    if (!memoria_worker || !memoria_worker->tablas_paginas) {
        log_debug(logger, "[FLUSH] No hay memoria inicializada o tablas de páginas");
        return -1;
    }
    
    log_info(logger, "[FLUSH] Persistiendo todas las páginas modificadas antes del desalojo...");
    
    int tablas_procesadas = 0;
    int total_paginas_escritas = 0;
    
    // Iterar sobre todas las tablas de páginas
    for (int i = 0; i < list_size(memoria_worker->tablas_paginas); i++) {
        t_tabla_paginas* tabla = list_get(memoria_worker->tablas_paginas, i);
        
        if (!tabla || !tabla->file_tag) {
            continue;
        }
        
        // Obtener páginas modificadas de esta tabla
        t_list* paginas_modificadas = memoria_get_paginas_modificadas(tabla->file_tag);
        
        if (list_is_empty(paginas_modificadas)) {
            list_destroy(paginas_modificadas);
            continue;
        }
        
        tablas_procesadas++;
        log_debug(logger, "[FLUSH] Procesando %s con %d páginas modificadas", 
                  tabla->file_tag, list_size(paginas_modificadas));
        
        // Escribir cada página modificada
        for (int j = 0; j < list_size(paginas_modificadas); j++) {
            t_pagina* pagina = list_get(paginas_modificadas, j);
            
            if (!pagina->presente) {
                continue;
            }
            
            // Obtener datos de la página desde memoria
            void* datos_pagina = memoria_get_datos_pagina(pagina->marco);
            if (!datos_pagina) {
                log_warning(logger, "[FLUSH] No se pudieron obtener datos de página %u", pagina->numero_pagina);
                continue;
            }
            
            // NUEVO PROTOCOLO PAGINACIÓN: [OP][file_len][file][tag_len][tag][block_num][block_data]
            // Separar file:tag
            char file_tag_copy2[256];
            strncpy(file_tag_copy2, tabla->file_tag, sizeof(file_tag_copy2) - 1);
            file_tag_copy2[sizeof(file_tag_copy2) - 1] = '\0';
            
            char *file = strtok(file_tag_copy2, ":");
            char *tag = strtok(NULL, ":");
            
            if (!file || !tag) {
                log_error(logger, "[FLUSH ALL] Error parseando file:tag '%s'", tabla->file_tag);
                continue;
            }
            
            uint8_t op = OP_STORAGE_WRITE;
            uint32_t query_id_net = htonl(current_query_id);
            uint32_t file_len = strlen(file);
            uint32_t tag_len = strlen(tag);
            uint32_t file_len_net = htonl(file_len);
            uint32_t tag_len_net = htonl(tag_len);
            uint32_t block_num_net = htonl(pagina->numero_pagina);
            
            // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num][block_data]
            // Enviar solicitud de escritura a Storage
            if (send(storage_fd, &op, 1, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, &query_id_net, 4, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, &file_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, file, file_len, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, tag, tag_len, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, &block_num_net, 4, MSG_NOSIGNAL) > 0 &&
                send(storage_fd, datos_pagina, memoria_worker->tamanio_pagina, MSG_NOSIGNAL) > 0) {
                
                // Esperar confirmación del Storage
                uint8_t response;
                if (recv(storage_fd, &response, 1, MSG_WAITALL) == 1 && response == ST_OK) {
                    // Marcar página como no modificada después de escribirla
                    pagina->modificada = false;
                    total_paginas_escritas++;
                    log_debug(logger, "[FLUSH] Página %s/%u persistida exitosamente", 
                              tabla->file_tag, pagina->numero_pagina);
                }
            }
        }
        
        list_destroy(paginas_modificadas);
    }
    
    if (total_paginas_escritas > 0) {
        log_info(logger, "[FLUSH] %d páginas persistidas de %d File:Tags antes del desalojo", 
                 total_paginas_escritas, tablas_procesadas);
    } else {
        log_debug(logger, "[FLUSH] No había páginas modificadas para persistir");
    }
    return 0;
}