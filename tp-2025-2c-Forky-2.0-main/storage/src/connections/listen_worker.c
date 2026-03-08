#include "connections.h"
#include <commons/log.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <utils/sockets.h>
#include <utils/protocol.h>
#include <pthread.h>
#include "../operations/storage_operations.h"  // Para storage_ctx

// Usar los valores del protocolo definidos en utils/protocol.h
extern t_log *STORAGE_LOG;
extern t_storage_context* storage_ctx;  // Contexto global de Storage

static int workers_conectados = 0;

static void *worker_thread(void *arg)
{
    worker_data_t *data = (worker_data_t *)arg;
    int fd = data->fd;
    uint32_t worker_id = data->id;

    log_info(STORAGE_LOG, "Worker %u hilo iniciado. punto_montaje='%s'", worker_id, data->punto_montaje);
    
    //free(arg); // Importante liberar el malloc de loop_aceptar_workers

    // Ahora tienes acceso al worker_id dentro del hilo

    while (1)
    {

        uint8_t op;
        if (recv(fd, &op, 1, 0) <= 0)
            break;


        if (op == OP_STORAGE_END)
        {
            log_info(STORAGE_LOG, "##Worker envió OP_STORAGE_END -> desconectando");
            break;
        }

        if (op == OP_STORAGE_READ)
        {
            log_info(STORAGE_LOG, "Recibido OP_STORAGE_READ");
            // === PROTOCOLO: [u8 OP_STORAGE_READ][u32 query_id][u32 file_len][file][u32 tag_len][tag][u32 block_num]
            uint32_t query_id_net, file_len_net, tag_len_net, block_num_net;
            
            // Leer query_id
            if (recv(fd, &query_id_net, 4, 0) <= 0) {
                break;
            }
            uint32_t query_id = ntohl(query_id_net);
            
            // Leer file
            if (recv(fd, &file_len_net, 4, 0) <= 0) {
                break;
            }
            uint32_t file_len = ntohl(file_len_net);

            char *file = malloc(file_len + 1);
            if (!file) {
                break;
            }
            if (recv_all(fd, file, file_len) < 0)
            {
                free(file);
                break;
            }
            file[file_len] = '\0';

            // Leer tag
            if (recv_all(fd, &tag_len_net, 4) <= 0)
            {
                free(file);
                break;
            }
            uint32_t tag_len = ntohl(tag_len_net);
            
            char *tag = malloc(tag_len + 1);
            if (!tag)
            {
                free(file);
                break;
            }
            if (recv_all(fd, tag, tag_len) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            tag[tag_len] = '\0';

            // Leer número de bloque (la página que solicita el Worker)
            if (recv_all(fd, &block_num_net, 4) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            uint32_t block_num = ntohl(block_num_net);

            log_info(STORAGE_LOG, "Worker solicita página/bloque %u del archivo %s:%s", block_num, file, tag);
            log_debug(STORAGE_LOG, "DEBUG READ: file_len=%u, file='%s', tag_len=%u, tag='%s', block_num=%u", 
                     file_len, file, tag_len, tag, block_num);

            // Leer el bloque específico usando storage_read_block
            char block_buffer[storage_ctx->block_size];
            memset(block_buffer, 0, storage_ctx->block_size);
            
            if (!storage_read_block(storage_ctx, file, tag, block_num, block_buffer, query_id)) {
                log_warning(STORAGE_LOG, "storage_read_block falló para %s:%s bloque %u - enviando ST_ERROR", file, tag, block_num);
                ssize_t sent = send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                if (sent <= 0) {
                    log_error(STORAGE_LOG, "Error enviando ST_ERROR al Worker (errno=%d: %s)", errno, strerror(errno));
                }
                free(file);
                free(tag);
                continue;
            }
            
            // Enviar respuesta: ST_OK + block_size + datos del bloque completo
            send(fd, (uint8_t[]){ST_OK}, 1, 0);
            uint32_t block_size_net = htonl(storage_ctx->block_size);
            send_all(fd, &block_size_net, 4);
            send_all(fd, block_buffer, storage_ctx->block_size);

            free(file);
            free(tag);
        }

        else if (op == OP_STORAGE_WRITE)
        {
            log_info(STORAGE_LOG, "Recibido OP_STORAGE_WRITE");
            // === PROTOCOLO: [u8 OP_STORAGE_WRITE][u32 query_id][u32 file_len][file][u32 tag_len][tag][u32 block_num][block_data]
            uint32_t query_id_net, file_len_net, tag_len_net, block_num_net;
            
            // Leer query_id
            if (recv(fd, &query_id_net, 4, 0) <= 0) {
                break;
            }
            uint32_t query_id = ntohl(query_id_net);
            
            // Leer file
            if (recv(fd, &file_len_net, 4, 0) <= 0) {
                break;
            }
            uint32_t file_len = ntohl(file_len_net);

            char *file = malloc(file_len + 1);
            if (!file) {
                break;
            }
            if (recv_all(fd, file, file_len) < 0)
            {
                free(file);
                break;
            }
            file[file_len] = '\0';

            // Leer tag
            if (recv_all(fd, &tag_len_net, 4) <= 0)
            {
                free(file);
                break;
            }
            uint32_t tag_len = ntohl(tag_len_net);
            
            char *tag = malloc(tag_len + 1);
            if (!tag)
            {
                free(file);
                break;
            }
            if (recv_all(fd, tag, tag_len) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            tag[tag_len] = '\0';

            // Leer número de bloque
            if (recv_all(fd, &block_num_net, 4) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            uint32_t block_num = ntohl(block_num_net);

            // Leer datos del bloque (siempre block_size bytes completos)
            char *block_buffer = malloc(storage_ctx->block_size);
            if (!block_buffer)
            {
                free(file);
                free(tag);
                break;
            }
            if (recv_all(fd, block_buffer, storage_ctx->block_size) < 0)
            {
                free(block_buffer);
                free(file);
                free(tag);
                break;
            }

            log_info(STORAGE_LOG, "Worker escribe página/bloque %u del archivo %s:%s", block_num, file, tag);

            // Escribir el bloque usando storage_write_block
            if (!storage_write_block(storage_ctx, file, tag, block_num, block_buffer, storage_ctx->block_size, query_id)) {
                log_warning(STORAGE_LOG, "[WORKER → STORAGE] storage_write_block rechazó escritura para %s:%s bloque %u", file, tag, block_num);
                send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
            } else {
                send(fd, (uint8_t[]){ST_OK}, 1, 0);
            }

            free(block_buffer);
            free(file);
            free(tag);
        }

        else if (op == OP_STORAGE_CREATE)
        {
            log_info(STORAGE_LOG, "Recibido OP_STORAGE_CREATE");
            uint32_t query_id_net, f_len_net, t_len_net, size_net;
            
            // Leer query_id
            if (recv_all(fd, &query_id_net, 4) <= 0)
                break;
            uint32_t query_id = ntohl(query_id_net);
            if (recv_all(fd, &f_len_net, 4) <= 0)
                break;
            uint32_t f_len = ntohl(f_len_net);
            char *file = malloc(f_len + 1);
            if (!file)
                break;
            if (recv_all(fd, file, f_len) < 0)
            {
                free(file);
                break;
            }
            file[f_len] = '\0';
            if (recv_all(fd, &t_len_net, 4) <= 0)
            {
                free(file);
                break;
            }
            uint32_t t_len = ntohl(t_len_net);
            char *tag = malloc(t_len + 1);
            if (!tag)
            {
                free(file);
                break;
            }
            if (recv_all(fd, tag, t_len) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            tag[t_len] = '\0';
            if (recv_all(fd, &size_net, 4) <= 0)
            {
                free(file);
                free(tag);
                break;
            }
            uint32_t size = ntohl(size_net);
            
            log_info(STORAGE_LOG, "##%u - File Creado %s:%s", query_id, file, tag);
            
            bool create_success = storage_create_file(storage_ctx, file, tag, query_id);
            uint8_t response = create_success ? ST_OK : ST_ERROR;
            
            if (!create_success) {
                log_error(STORAGE_LOG, "\x1b[1;31m[STORAGE] Error en storage_create_file para %s:%s - Enviando ST_ERROR (0x%02X)\x1b[0m", file, tag, ST_ERROR);
            } else {
                log_info(STORAGE_LOG, "\x1b[1;32m[STORAGE] Archivo de tag '%s' creado correctamente - Enviando ST_OK (0x%02X)\x1b[0m", tag, ST_OK);
            }

            // Responder al Worker con ST_OK o ST_ERROR
            if (send(fd, &response, 1, 0) < 0)
            {
                log_error(STORAGE_LOG, "[STORAGE] Error al enviar respuesta CREATE a Worker");
                free(file);
                free(tag);
                break;
            }

            free(file);
            free(tag);
        }

        else if (op == OP_STORAGE_TRUNCATE)
        {
            log_info(STORAGE_LOG, "Recibido OP_STORAGE_TRUNCATE");
            uint32_t query_id_net, f_len_net, t_len_net, new_size_net;

            if (recv_all(fd, &query_id_net, 4) <= 0)
                break;
            uint32_t query_id = ntohl(query_id_net);

            /* ----- file ----- */
            if (recv_all(fd, &f_len_net, 4) <= 0)
                break;
            uint32_t f_len = ntohl(f_len_net);
            char *file = malloc(f_len + 1);
            if (!file)
                break;
            if (recv_all(fd, file, f_len) < 0)
            {
                free(file);
                break;
            }
            file[f_len] = '\0';

            /* ----- tag  (ESTO TE FALTABA) ----- */
            if (recv_all(fd, &t_len_net, 4) <= 0)
            {
                free(file);
                break;
            }
            uint32_t t_len = ntohl(t_len_net);
            char *tag = malloc(t_len + 1);
            if (!tag)
            {
                free(file);
                break;
            }
            if (recv_all(fd, tag, t_len) < 0)
            {
                free(file);
                free(tag);
                break;
            }
            tag[t_len] = '\0';

            /* ----- new_size ----- */
            if (recv_all(fd, &new_size_net, 4) <= 0)
            {
                free(file);
                free(tag);
                break;
            }
            uint32_t new_size = ntohl(new_size_net);

            log_info(STORAGE_LOG, "##%u - File Truncado %s:%s - Tamaño: %u", query_id, file, tag, new_size);

            if (!storage_truncate_file(storage_ctx, file, tag, new_size, query_id)) {
                log_error(STORAGE_LOG, "Error en storage_truncate_file para %s:%s", file, tag);
            }

            if (send(fd, (uint8_t[]){ST_OK}, 1, 0) < 0)
            {
                free(file);
                free(tag);
                break;
            }

            free(file);
            free(tag);
        }

        // ========== OP_STORAGE_TAG ==========
            else if (op == OP_STORAGE_TAG)
            {
                log_info(STORAGE_LOG, "Recibido OP_STORAGE_TAG");

                // Leer query_id primero
                uint32_t query_id_net;
                uint32_t query_id;
                if (recv_all(fd, &query_id_net, 4) <= 0) {
                    log_error(STORAGE_LOG, "Error recibiendo query_id del TAG");
                    send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    break;
                }

                query_id = ntohl(query_id_net);

                char *file_orig, *tag_orig, *file_dst, *tag_dst;

                if (protocol_recv_worker_tag(fd, &file_orig, &tag_orig, &file_dst, &tag_dst) < 0) {
                    log_error(STORAGE_LOG, "Error recibiendo datos del TAG");
                    send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    break;
                }

                log_info(STORAGE_LOG, "##%u - TAG %s:%s -> %s:%s", query_id, file_orig, tag_orig, file_dst, tag_dst);

                if (!storage_tag_file(storage_ctx, file_orig, tag_orig, file_dst, tag_dst, query_id)) {
                    log_error(STORAGE_LOG, "Error en storage_tag_file");
                    send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    goto cleanup_tag;
                }

                // Enviar confirmación al Worker
                if (send(fd, (uint8_t[]){ST_OK}, 1, MSG_NOSIGNAL) < 0) {
                    log_error(STORAGE_LOG, "Error enviando ST_OK al Worker (errno=%d)", errno);
                } else {
                    log_debug(STORAGE_LOG, "ST_OK enviado correctamente al Worker (fd=%d)", fd);
                }

                cleanup_tag:
                free(file_orig);
                free(tag_orig);
                free(file_dst);
                free(tag_dst);
            }

            // ========== DELETE ==========
            else if (op == OP_STORAGE_DELETE)
            {
                log_info(STORAGE_LOG, "Recibido OP_STORAGE_DELETE");

                uint32_t file_tag_len_net;
                if (recv_all(fd, &file_tag_len_net, 4) <= 0) {
                    log_error(STORAGE_LOG, "[DELETE] Error recibiendo longitud de file:tag");
                    break;
                }

                uint32_t file_tag_len = ntohl(file_tag_len_net);
                char *file_tag = malloc(file_tag_len + 1);
                if (!file_tag) {
                    log_error(STORAGE_LOG, "[DELETE] Error al allocar memoria para file:tag");
                    break;
                }

                if (recv_all(fd, file_tag, file_tag_len) < 0) {
                    log_error(STORAGE_LOG, "[DELETE] Error recibiendo file:tag");
                    free(file_tag);
                    break;
                }
                file_tag[file_tag_len] = '\0';

                log_info(STORAGE_LOG, "DELETE recibido para tag %s", file_tag);

                // Parsear "FILE:TAG"
                char *colon = strchr(file_tag, ':');
                if (!colon) {
                    log_error(STORAGE_LOG, "[DELETE] Formato inválido: '%s'. Esperado FILE:TAG", file_tag);
                    send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    free(file_tag);
                    continue;
                }

                *colon = '\0';
                char *file_name = file_tag;
                char *tag_name  = colon + 1;

                // Podés usar query_id = 0 o no loguearlo, ya que Worker no manda query_id
                if (!storage_delete_tag(storage_ctx, file_name, tag_name, 0)) {
                    log_error(STORAGE_LOG, "[DELETE] Error en storage_delete_tag para %s:%s", file_name, tag_name);
                    send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                } else {
                    log_info(STORAGE_LOG, "Tag %s:%s eliminado correctamente", file_name, tag_name);
                    send(fd, (uint8_t[]){ST_OK}, 1, 0);
                }

                free(file_tag);
            }

            // ========== FLUSH / COMMIT ==========
            else if (op == OP_STORAGE_FLUSH || op == OP_STORAGE_COMMIT)
            {
                const char *op_name = (op == OP_STORAGE_FLUSH) ? "FLUSH" : "COMMIT";

                log_info(STORAGE_LOG, "Recibido %s", op_name);

                // Leer query_id
                uint32_t query_id_net;
                if (recv(fd, &query_id_net, 4, 0) <= 0)
                    break;
                uint32_t query_id = ntohl(query_id_net);

                // Leer file
                uint32_t file_len_net;
                if (recv(fd, &file_len_net, 4, 0) <= 0)
                    break;

                uint32_t file_len = ntohl(file_len_net);
                char *file = malloc(file_len + 1);
                if (!file)
                    break;

                if (recv_all(fd, file, file_len) < 0) {
                    free(file);
                    break;
                }
                file[file_len] = '\0';

                char *tag = NULL;
                if (op == OP_STORAGE_COMMIT || op == OP_STORAGE_FLUSH) {
                    uint32_t tag_len_net;
                    if (recv(fd, &tag_len_net, 4, 0) <= 0) {
                        free(file);
                        break;
                    }

                    uint32_t tag_len = ntohl(tag_len_net);
                    tag = malloc(tag_len + 1);
                    if (!tag) {
                        free(file);
                        break;
                    }

                    if (recv_all(fd, tag, tag_len) < 0) {
                        free(file);
                        free(tag);
                        break;
                    }
                    tag[tag_len] = '\0';
                }

                if (op == OP_STORAGE_COMMIT) {
                    log_info(STORAGE_LOG, "##%u - Commit de File:Tag %s:%s", query_id, file, tag);
                    if (storage_commit_tag(storage_ctx, file, tag, query_id)) {
                        send(fd, (uint8_t[]){ST_OK}, 1, 0);
                    } else {
                        send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    }
                    free(tag);
                } else { // FLUSH
                    log_info(STORAGE_LOG, "##%u - FLUSH '%s%s%s'", query_id, file, tag ? ":" : "", tag ? tag : "");
                    if (storage_flush_tag(storage_ctx, file, tag)) {
                        send(fd, (uint8_t[]){ST_OK}, 1, 0);
                    } else {
                        send(fd, (uint8_t[]){ST_ERROR}, 1, 0);
                    }
                    if (tag) free(tag);
                }

                free(file);
            }

    }

    shutdown(fd, SHUT_WR);
    close(fd);
    
    // Decrementar contador y log de desconexión
    workers_conectados--;
    log_info(STORAGE_LOG, "\x1b[31m## Se desconecta el Worker %u - Cantidad de Workers: %d\x1b[0m", 
             worker_id, workers_conectados);

    free((void*)data->punto_montaje);
    free(data);
    
    return NULL;
}

void loop_aceptar_workers(int server_fd, uint32_t block_size, const char* punto_montaje)
{
    while (1)
    {
        // aceptar próximo Worker
        int fd = listen_server(STORAGE_LOG, server_fd, (char *)"Worker");
        if (fd < 0)
        {
            log_error(STORAGE_LOG, "listen_server fallo: %s", strerror(errno));
            continue;
        }

        // handshake: espera OP_STORAGE_WORKER_HANDSHAKE + worker_id
        uint32_t worker_id = 0;

        if (protocol_recv_worker_handshake(fd, &worker_id) < 0) {
            log_warning(STORAGE_LOG, "Handshake inválido de Worker");
            send(fd, (uint8_t[]){ST_ERROR}, 1, 0); // enviar ST_ERROR
            close(fd);
            continue;
        }

        workers_conectados++;
        log_info(STORAGE_LOG, "\x1b[32m## Se conecta el Worker %u - Cantidad de Workers: %d\x1b[0m",
                 worker_id, workers_conectados);

        // respuesta OK + BLOCK_SIZE
        if (protocol_send_storage_handshake_ok(fd, block_size) < 0)
        {
            log_error(STORAGE_LOG, "Error enviando handshake OK al Worker %u", worker_id);
            close(fd);
            workers_conectados--;
            continue;
        }

        log_info(STORAGE_LOG, "Handshake OK enviado al Worker %u", worker_id);

        worker_data_t *data = malloc(sizeof(worker_data_t));
        if (!data)
        {
            log_error(STORAGE_LOG, "Error al asignar memoria para worker_data_t");
            close(fd);
            workers_conectados--;
            continue;
        }

        data->fd = fd;
        data->id = worker_id;
        data->punto_montaje = strdup(punto_montaje);   // <— copia propia para el hilo
        if (!data->punto_montaje) {
            log_error(STORAGE_LOG, "No se pudo duplicar punto_montaje");
            close(fd);
            free(data);
            workers_conectados--;
            continue;
        }

        // Crear el hilo, pasando la estructura
        pthread_t th;
        pthread_create(&th, NULL, worker_thread, data);
        pthread_detach(th);
        // NO cerramos fd acá; lo cierra el hilo cuando el worker se vaya
    }
}