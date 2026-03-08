#include "file_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <commons/string.h>

// Gestor global de locks FILE:TAG (diccionario de mutexes)
static t_dictionary* file_tag_locks = NULL;
static pthread_mutex_t lock_manager_mtx = PTHREAD_MUTEX_INITIALIZER;

// Función auxiliar para crear directorio si no existe
static bool ensure_directory(const char* path, t_log* logger) {
    struct stat st = {0};
    
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) == -1) {
            log_error(logger, "Error al crear directorio '%s': %s", path, strerror(errno));
            return false;
        }
        log_trace(logger, "Directorio creado: %s", path);
    }
    
    return true;
}

// Crea el directorio para un File
bool create_file_directory(const char* punto_montaje, const char* file_name, t_log* logger) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", punto_montaje, file_name);
    
    return ensure_directory(file_path, logger);
}

// Crea el directorio para un Tag dentro de un File
bool create_tag_directory(const char* punto_montaje, const char* file_name, 
                          const char* tag_name, t_log* logger) {
    char tag_path[512];
    char metadata_path[512];
    char logical_blocks_path[512];
    
    // Crear directorio del File si no existe
    if (!create_file_directory(punto_montaje, file_name, logger)) {
        return false;
    }
    
    // Crear directorio del Tag
    snprintf(tag_path, sizeof(tag_path), "%s/files/%s/%s", punto_montaje, file_name, tag_name);
    if (!ensure_directory(tag_path, logger)) {
        return false;
    }
    
    // Crear metadata.config vacío
    snprintf(metadata_path, sizeof(metadata_path), "%s/metadata.config", tag_path);
    FILE* f = fopen(metadata_path, "w");
    if (!f) {
        log_error(logger, "Error al crear metadata.config: %s", strerror(errno));
        return false;
    }
    
    // Crear metadata inicial: tamaño 0, sin bloques, WORK_IN_PROGRESS
    fprintf(f, "TAMAÑO=0\n");
    fprintf(f, "BLOCKS=[]\n");
    fprintf(f, "ESTADO=WORK_IN_PROGRESS\n");
    fclose(f);
    
    // Crear directorio logical_blocks/
    snprintf(logical_blocks_path, sizeof(logical_blocks_path), "%s/logical_blocks", tag_path);
    if (!ensure_directory(logical_blocks_path, logger)) {
        return false;
    }
    
    log_trace(logger, "[DEBUG] Tag creado en FS: %s/%s", file_name, tag_name);
    return true;
}

// Lee el metadata de un File:Tag
t_file_tag_metadata* read_metadata(const char* punto_montaje, const char* file_name, 
                                   const char* tag_name, t_log* logger) {
    char metadata_path[512];
    snprintf(metadata_path, sizeof(metadata_path), "%s/files/%s/%s/metadata.config", 
             punto_montaje, file_name, tag_name);
    
    t_config* config = config_create(metadata_path);
    if (!config) {
        log_warning(logger,"[STORAGE] metadata.config no existe todavía para %s/%s (WORK_IN_PROGRESS)",file_name, tag_name);
    return NULL;
    }
    
    t_file_tag_metadata* metadata = malloc(sizeof(t_file_tag_metadata));
    if (!metadata) {
        config_destroy(config);
        return NULL;
    }
    
    // Leer TAMAÑO
    metadata->tamanio = (uint32_t)config_get_int_value(config, "TAMAÑO");
    
    // Leer BLOCKS como array
    char** blocks_array = config_get_array_value(config, "BLOCKS");
    metadata->blocks = list_create();
    
    if (blocks_array) {
        for (int i = 0; blocks_array[i] != NULL; i++) {
            uint32_t* block_num = malloc(sizeof(uint32_t));
            *block_num = (uint32_t)atoi(blocks_array[i]);
            list_add(metadata->blocks, block_num);
        }
        
        // Liberar el array
        for (int i = 0; blocks_array[i] != NULL; i++) {
            free(blocks_array[i]);
        }
        free(blocks_array);
    }
    
    // Leer ESTADO
    char* estado_str = config_get_string_value(config, "ESTADO");
    if (strcmp(estado_str, "COMMITED") == 0) {
        metadata->estado = ESTADO_COMMITED;
    } else {
        metadata->estado = ESTADO_WORK_IN_PROGRESS;
    }
    
    // Guardar nombres
    metadata->file_name = strdup(file_name);
    metadata->tag_name = strdup(tag_name);
    metadata->metadata_path = strdup(metadata_path);
    
    config_destroy(config);
    
    log_trace(logger, "Metadata leído: %s/%s (tamaño=%u, bloques=%d, estado=%s)",
              file_name, tag_name, metadata->tamanio, list_size(metadata->blocks),
              metadata->estado == ESTADO_COMMITED ? "COMMITED" : "WORK_IN_PROGRESS");
    
    return metadata;
}

// Escribe el metadata al disco
bool write_metadata(t_file_tag_metadata* metadata, t_log* logger) {
    FILE* f = fopen(metadata->metadata_path, "w");
    if (!f) {
        log_error(logger, "Error al escribir metadata.config: %s", strerror(errno));
        return false;
    }
    
    // Escribir TAMAÑO
    fprintf(f, "TAMAÑO=%u\n", metadata->tamanio);
    
    // Escribir BLOCKS
    fprintf(f, "BLOCKS=[");
    int size = list_size(metadata->blocks);
    for (int i = 0; i < size; i++) {
        uint32_t* block_num = list_get(metadata->blocks, i);
        fprintf(f, "%u", *block_num);
        if (i < size - 1) {
            fprintf(f, ",");
        }
    }
    fprintf(f, "]\n");
    
    // Escribir ESTADO
    fprintf(f, "ESTADO=%s\n", 
            metadata->estado == ESTADO_COMMITED ? "COMMITED" : "WORK_IN_PROGRESS");
    
    fclose(f);
    
    log_trace(logger, "Metadata escrito: %s/%s", metadata->file_name, metadata->tag_name);
    return true;
}

// Destruye el metadata
void file_metadata_destroy(t_file_tag_metadata* metadata) {
    if (!metadata) return;
    
    // Liberar la lista de bloques
    if (metadata->blocks) {
        list_destroy_and_destroy_elements(metadata->blocks, free);
    }
    
    free(metadata->file_name);
    free(metadata->tag_name);
    free(metadata->metadata_path);
    free(metadata);
}

// Crea un hard link de bloque lógico a bloque físico
bool create_logical_block_link(const char* punto_montaje, const char* file_name,
                               const char* tag_name, uint32_t logical_block_num,
                               uint32_t physical_block_num, t_log* logger, uint32_t query_id) {
    char logical_path[512];
    char physical_path[512];
    
    snprintf(logical_path, sizeof(logical_path), 
             "%s/files/%s/%s/logical_blocks/%06u.dat",
             punto_montaje, file_name, tag_name, logical_block_num);
    
    snprintf(physical_path, sizeof(physical_path),
             "%s/physical_blocks/block%04u.dat",
             punto_montaje, physical_block_num);
    
    // Verificar si el bloque físico existe
    if (access(physical_path, F_OK) != 0) {
        log_error(logger, "Error: Bloque físico %u no existe en %s", physical_block_num, physical_path);
        return false;
    }
    
    // Eliminar hard link anterior si existe
    unlink(logical_path);
    
    // Crear hard link
    if (link(physical_path, logical_path) != 0) {
        log_error(logger, "Error al crear hard link %s -> %s: %s", logical_path, physical_path, strerror(errno));
        return false;
    }
    
    // Log obligatorio con id query
    log_info(logger, "\x1b[32m##%u - %s:%s Se agregó el hard link del bloque lógico %u al bloque físico %u\x1b[0m", query_id, file_name, tag_name, logical_block_num, physical_block_num);
    return true;
}

// Elimina un hard link de bloque lógico
bool remove_logical_block_link(const char* punto_montaje, const char* file_name,
                               const char* tag_name, uint32_t logical_block_num,
                               uint32_t physical_block_num, t_log* logger, uint32_t query_id) {
    char logical_path[512];
    
    snprintf(logical_path, sizeof(logical_path),
             "%s/files/%s/%s/logical_blocks/%06u.dat",
             punto_montaje, file_name, tag_name, logical_block_num);
    
    // Verificar si el hard link existe antes de intentar eliminarlo
    if (access(logical_path, F_OK) != 0) {
        log_warning(logger, "Hard link %s no existe, no se puede eliminar", logical_path);
        return true; // No es un error crítico
    }

    if (unlink(logical_path) != 0) {
        log_error(logger, "Error al eliminar hard link %s: %s", logical_path, strerror(errno));
        return false;
    }
    
    // Log obligatorio con bloque físico
    log_info(logger, "\x1b[32m##%u - %s:%s Se eliminó el hard link del bloque lógico %u al bloque físico %u\x1b[0m", query_id, file_name, tag_name, logical_block_num, physical_block_num);
    return true;
}

// Verifica si un File existe
bool file_exists(const char* punto_montaje, const char* file_name) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", punto_montaje, file_name);
    
    struct stat st;
    return (stat(file_path, &st) == 0 && S_ISDIR(st.st_mode));
}

// Verifica si un Tag existe
bool tag_exists(const char* punto_montaje, const char* file_name, const char* tag_name) {
    char tag_path[512];
    snprintf(tag_path, sizeof(tag_path), "%s/files/%s/%s", punto_montaje, file_name, tag_name);
    
    struct stat st;
    return (stat(tag_path, &st) == 0 && S_ISDIR(st.st_mode));
}

// Inicializa el gestor global de locks FILE:TAG
void file_tag_lock_manager_init(void) {
    pthread_mutex_lock(&lock_manager_mtx);
    if (!file_tag_locks) {
        file_tag_locks = dictionary_create();
    }
    pthread_mutex_unlock(&lock_manager_mtx);
}

// Destruye el gestor global de locks FILE:TAG
void file_tag_lock_manager_destroy(void) {
    pthread_mutex_lock(&lock_manager_mtx);
    if (file_tag_locks) {
        // Liberar todos los mutexes del diccionario
        t_list* keys = dictionary_keys(file_tag_locks);
        for (int i = 0; i < list_size(keys); i++) {
            char* key = list_get(keys, i);
            pthread_mutex_t* mtx = (pthread_mutex_t*)dictionary_remove(file_tag_locks, key);
            if (mtx) {
                pthread_mutex_destroy(mtx);
                free(mtx);
            }
        }
        list_destroy(keys);
        dictionary_destroy(file_tag_locks);
        file_tag_locks = NULL;
    }
    pthread_mutex_unlock(&lock_manager_mtx);
}

// Adquiere el lock para un FILE:TAG (crea el mutex si no existe)
void lock_file_tag(const char* file_name, const char* tag_name) {
    // Construir clave único: "file:tag"
    char key[512];
    snprintf(key, sizeof(key), "%s:%s", file_name, tag_name);
    
    // Proteger acceso al diccionario
    pthread_mutex_lock(&lock_manager_mtx);
    
    // Si no existe, crear el mutex
    if (!dictionary_has_key(file_tag_locks, key)) {
        pthread_mutex_t* mtx = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(mtx, NULL);
        dictionary_put(file_tag_locks, key, mtx);
    }
    
    // Obtener el mutex
    pthread_mutex_t* mtx = (pthread_mutex_t*)dictionary_get(file_tag_locks, key);
    pthread_mutex_unlock(&lock_manager_mtx);
    
    // Bloquear el mutex del FILE:TAG
    pthread_mutex_lock(mtx);
}

// Libera el lock para un FILE:TAG
void unlock_file_tag(const char* file_name, const char* tag_name) {
    // Construir clave único
    char key[512];
    snprintf(key, sizeof(key), "%s:%s", file_name, tag_name);
    
    // Proteger acceso al diccionario
    pthread_mutex_lock(&lock_manager_mtx);
    
    if (dictionary_has_key(file_tag_locks, key)) {
        pthread_mutex_t* mtx = (pthread_mutex_t*)dictionary_get(file_tag_locks, key);
        pthread_mutex_unlock(&lock_manager_mtx);
        // Desbloquear
        pthread_mutex_unlock(mtx);
    } else {
        pthread_mutex_unlock(&lock_manager_mtx);
    }
}
