#ifndef STORAGE_CONNECTIONS_H
#define STORAGE_CONNECTIONS_H

#include <commons/log.h>
#include <utils/protocol.h>
#include <stdint.h>
#include <pthread.h>
typedef struct {
    int fd;
    uint32_t id;
    char* punto_montaje;
} worker_data_t;

void loop_aceptar_workers(int server_fd, uint32_t block_size, const char* punto_montaje);

int create_storage_server(t_log *logger, int listen_port);

int accept_worker_client(t_log *logger, int server_fd);

void serve_worker_handshake(t_log *logger, int worker_fd, uint32_t block_size);

#endif