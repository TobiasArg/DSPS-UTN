#ifndef STORAGE_MAIN_H
#define STORAGE_MAIN_H

#include <commons/log.h>
#include <stdint.h>

typedef struct {
    int      listen_port;          // PUERTO_ESCUCHA
    char*    mount_point;          // PUNTO_MONTAJE
    int      fresh_start;          // FRESH_START
    int      retardo_operacion;    // RETARDO_OPERACION
    int      retardo_acceso;       // RETARDO_ACCESO_BLOQUE
    uint32_t block_size;           // leído de superblock.config
} t_config_storage;

#endif 