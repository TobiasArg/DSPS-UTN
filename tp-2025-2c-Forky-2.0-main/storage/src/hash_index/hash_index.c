#include "hash_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

// Inicializa el hash index
t_hash_index* hash_index_init(const char* punto_montaje, t_log* logger) {
    t_hash_index* index = malloc(sizeof(t_hash_index));
    if (!index) {
        log_error(logger, "Error al asignar memoria para hash index");
        return NULL;
    }
    
    index->logger = logger;
    index->hash_to_block = dictionary_create();
    
    // Construir ruta del índice
    index->index_path = malloc(512);
    snprintf(index->index_path, 512, "%s/blocks_hash_index.config", punto_montaje);
    
    // Verificar que el directorio exista
    char dir_path[512];
    snprintf(dir_path, 512, "%s", punto_montaje);
    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
        if (mkdir(dir_path, 0755) == -1) {
            log_error(logger, "Error al crear directorio %s: %s", dir_path, strerror(errno));
            free(index->index_path);
            dictionary_destroy(index->hash_to_block);
            free(index);
            return NULL;
        }
    }

    // Leer/crear el archivo blocks_hash_index.config
    FILE* f = fopen(index->index_path, "a+"); // a+ crea si no existe, lee si existe
    if (!f) {
        log_error(logger, "Error al abrir/crear blocks_hash_index.config: %s", strerror(errno));
        // Intentar crear directamente
        f = fopen(index->index_path, "w+");
        if (!f) {
            log_error(logger, "Error crítico: no se puede crear blocks_hash_index.config");
            free(index->index_path);
            dictionary_destroy(index->hash_to_block);
            free(index);
            return NULL;
        }
    }
    
    // Ir al inicio para leer
    rewind(f);
    
    // Leer línea por línea: hash=blockXXXX
    char line[256];
    int entries_loaded = 0;
    
    while (fgets(line, sizeof(line), f)) {
        // Eliminar salto de línea
        line[strcspn(line, "\n")] = 0;
        
        // Parsear: hash=blockXXXX
        char* equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* hash = line;
        char* block_str = equals + 1;
        
        // Agregar al diccionario (hacer copia del valor)
        dictionary_put(index->hash_to_block, hash, strdup(block_str));
        entries_loaded++;
    }
    
    fclose(f);
    
    // Inicializar mutex para sincronización
    pthread_mutex_init(&index->mtx, NULL);
   
    log_info(logger, "Hash index inicializado: %d entradas cargadas", entries_loaded);
    
    return index;
}

// Función auxiliar estática para liberar valores
static void free_value_helper(void* value) {
    free(value);
}

// Destruye el hash index
void hash_index_destroy(t_hash_index* index) {
    if (!index) return;
    
    // Sincronizar antes de destruir
    hash_index_sync_to_disk(index);
    
    // Destruir mutex
    pthread_mutex_destroy(&index->mtx);
   
    // Liberar valores del diccionario
    dictionary_destroy_and_destroy_elements(index->hash_to_block, free_value_helper);
    
    free(index->index_path);
    free(index);
}

// Busca un hash y devuelve el número de bloque
int32_t hash_index_find_block(t_hash_index* index, const char* hash) {
    if (!dictionary_has_key(index->hash_to_block, (char*)hash)) {
        return -1;
    }
    
    char* block_str = dictionary_get(index->hash_to_block, (char*)hash);
    
    // Parsear "blockXXXX" para obtener el número
    uint32_t block_num;
    if (sscanf(block_str, "block%u", &block_num) == 1) {
        return (int32_t)block_num;
    }
    
    return -1;
}

// Agrega una entrada hash -> bloque
bool hash_index_add_entry(t_hash_index* index, const char* hash, uint32_t block_num) {
    char block_str[16];
    snprintf(block_str, sizeof(block_str), "block%04u", block_num);
    
    // Si ya existe, liberar el valor anterior
    if (dictionary_has_key(index->hash_to_block, (char*)hash)) {
        char* old_value = dictionary_remove(index->hash_to_block, (char*)hash);
        free(old_value);
    }
    
    dictionary_put(index->hash_to_block, (char*)hash, strdup(block_str));
    
    log_trace(index->logger, "Hash index: %s -> %s", hash, block_str);
    
    return true;
}

// Elimina una entrada del índice
bool hash_index_remove_entry(t_hash_index* index, const char* hash) {
    if (!dictionary_has_key(index->hash_to_block, (char*)hash)) {
        return false;
    }
    
    char* value = dictionary_remove(index->hash_to_block, (char*)hash);
    free(value);
    
    log_trace(index->logger, "Hash index: eliminado %s", hash);
    
    return true;
}

// Sincroniza el índice a disco
bool hash_index_sync_to_disk(t_hash_index* index) {
    FILE* f = fopen(index->index_path, "w");
    if (!f) {
        log_error(index->logger, "Error al abrir blocks_hash_index.config para escritura: %s", 
                  strerror(errno));
        return false;
    }
    
    // Usar un enfoque simple: iterar manualmente sobre el diccionario
    // Como dictionary_iterator no permite pasar contexto, escribiremos directamente
    t_list* keys = dictionary_keys(index->hash_to_block);
    
    for (int i = 0; i < list_size(keys); i++) {
        char* key = list_get(keys, i);
        char* value = dictionary_get(index->hash_to_block, key);
        fprintf(f, "%s=%s\n", key, value);
    }
    
    list_destroy(keys);
    fclose(f);
    
    log_trace(index->logger, "Hash index sincronizado a disco");
    
    return true;
}

// Cuenta cuántas veces un bloque está referenciado
uint32_t hash_index_count_block_references(t_hash_index* index, uint32_t block_num) {
    char target_block[16];
    snprintf(target_block, sizeof(target_block), "block%04u", block_num);
    
    uint32_t count = 0;
    
    // Iterar manualmente sobre todas las entradas
    t_list* keys = dictionary_keys(index->hash_to_block);
    
    for (int i = 0; i < list_size(keys); i++) {
        char* key = list_get(keys, i);
        char* value = dictionary_get(index->hash_to_block, key);
        
        if (strcmp(value, target_block) == 0) {
            count++;
        }
    }
    
    list_destroy(keys);
    
    return count;
}
