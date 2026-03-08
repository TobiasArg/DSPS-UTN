#include <commons/config.h>
#include <commons/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <utils/sockets.h>         
#include "connections/connections.h"
#include "fs_init/fs_init.h"
#include "bitmap/bitmap_manager.h"   // Agregar bitmap manager
#include "operations/storage_operations.h"  // Para storage_context

t_log* STORAGE_LOG;
t_bitmap_manager* bitmap_manager = NULL;  // Variable global para bitmap
t_storage_context* storage_ctx = NULL;    // Contexto completo de Storage

// Variables globales para retardos (usadas en operaciones)
uint32_t RETARDO_OPERACION = 0;
uint32_t RETARDO_ACCESO_BLOQUE = 0;

static uint32_t leer_block_size(const char* punto){
    char path[512];
    snprintf(path, sizeof(path), "%s/superblock.config", punto);
    t_config* sb = config_create(path);
    if (!sb) {
        return 128;
    }
    uint32_t bs = (uint32_t) config_get_int_value(sb, "BLOCK_SIZE");
    config_destroy(sb);
    return bs;
}

int main(int argc, char** argv) {
    const char* cfg_path = (argc > 1) ? argv[1] : "./storage.config";
    STORAGE_LOG = log_create("storage.log", "Storage", 1, LOG_LEVEL_INFO);

    // Cargar configuración
    t_config* cfg = config_create((char*)cfg_path);
    if (!cfg) {
        perror("Error al abrir storage.config");
        return 1;
    }

    // Leer parámetros del config
    int puerto = config_get_int_value(cfg, "PUERTO_ESCUCHA");
    char* fresh_start_str = config_get_string_value(cfg, "FRESH_START");
    bool fresh_start = (strcmp(fresh_start_str, "TRUE") == 0 || strcmp(fresh_start_str, "true") == 0);
    
    // Obtener valor del punto de montaje y duplicarlo
    const char* punto_tmp = config_get_string_value(cfg, "PUNTO_MONTAJE");
    char* punto = strdup(punto_tmp);  // hacemos una copia segura del string

    // Leer retardos del config
    RETARDO_OPERACION = (uint32_t) config_get_int_value(cfg, "RETARDO_OPERACION");
    RETARDO_ACCESO_BLOQUE = (uint32_t) config_get_int_value(cfg, "RETARDO_ACCESO_BLOQUE");
    
    log_info(STORAGE_LOG, "Retardos configurados: OPERACION=%ums, ACCESO_BLOQUE=%ums", 
             RETARDO_OPERACION, RETARDO_ACCESO_BLOQUE);

    // Crear directorio raíz si no existe
    if (mkdir(punto, 0755) == -1 && errno != EEXIST) {
        perror("mkdir PUNTO_MONTAJE");
        free(punto);
        return 1;
    }

    // Crear directorio Files
    char files_path[512];
    snprintf(files_path, sizeof(files_path), "%s/Files", punto);
    if (mkdir(files_path, 0755) == -1 && errno != EEXIST) {
        log_warning(STORAGE_LOG, "No se pudo crear directorio Files/ - continuando...");
    }

    // Inicializar el File System
    if (!fs_init(punto, fresh_start, STORAGE_LOG)) {
        log_error(STORAGE_LOG, "Error al inicializar el File System");
        free(punto);
        config_destroy(cfg);
        return 1;
    }

    uint32_t block_size = leer_block_size(punto);
    
    // Inicializar bitmap manager para FASE 2
    // Leer número de bloques del superblock
    char sb_path[512];
    snprintf(sb_path, sizeof(sb_path), "%s/superblock.config", punto);
    t_config* sb = config_create(sb_path);
    if (!sb) {
        log_error(STORAGE_LOG, "Error al leer superblock para bitmap manager");
        free(punto);
        config_destroy(cfg);
        return 1;
    }
    
    uint32_t fs_size = (uint32_t) config_get_int_value(sb, "FS_SIZE");
    uint32_t num_blocks = fs_size / block_size;
    config_destroy(sb);
    
    bitmap_manager = bitmap_manager_init(punto, num_blocks, STORAGE_LOG);
    if (!bitmap_manager) {
        log_error(STORAGE_LOG, "Error al inicializar bitmap manager");
        free(punto);
        config_destroy(cfg);
        return 1;
    }
    
    log_info(STORAGE_LOG, "Bitmap manager inicializado: %u bloques totales", num_blocks);
    log_info(STORAGE_LOG, "Bloques libres: %u", bitmap_count_free_blocks(bitmap_manager));
    
    // Crear blocks_hash_index.config si no existe
    char hash_index_path[512];
    snprintf(hash_index_path, sizeof(hash_index_path), "%s/blocks_hash_index.config", punto);
    FILE* hash_file = fopen(hash_index_path, "a");
    if (hash_file) {
        fclose(hash_file);
        log_info(STORAGE_LOG, "blocks_hash_index.config verificado/creado");
    } else {
        log_warning(STORAGE_LOG, "No se pudo crear blocks_hash_index.config");
    }

    // Inicializar contexto de Storage 
    storage_ctx = storage_context_init(punto, block_size, num_blocks, STORAGE_LOG);
    if (!storage_ctx) {
        log_error(STORAGE_LOG, "Error al inicializar contexto de Storage");
        bitmap_manager_destroy(bitmap_manager);
        free(punto);
        config_destroy(cfg);
        return 1;
    }
    
    log_info(STORAGE_LOG, "Contexto de Storage inicializado - FASES 2 y 3 operativas");

    char puerto_str[16];
    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto);
    int server_fd = start_server(STORAGE_LOG, "0.0.0.0", puerto_str);

    log_info(STORAGE_LOG, "Storage escuchando en %d, montaje=%s (BLOCK_SIZE=%u)",
            puerto, punto, block_size);

    // Ahora sí podés liberar el config sin perder la ruta
    config_destroy(cfg);

    log_info(STORAGE_LOG, "Iniciando loop_aceptar_workers con punto_montaje='%s'", punto);

    loop_aceptar_workers(server_fd, block_size, punto);

    // Cleanup storage context y bitmap manager
    if (storage_ctx) {
        storage_context_destroy(storage_ctx);
        storage_ctx = NULL;
    }
    
    if (bitmap_manager) {
        bitmap_manager_destroy(bitmap_manager);
        bitmap_manager = NULL;
    }

    // Liberamos la copia al final
    free(punto);
    return 0;
}
