#include "bitmap_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

// Inicializa el bitmap manager
t_bitmap_manager* bitmap_manager_init(const char* punto_montaje, uint32_t num_blocks, t_log* logger) {
    t_bitmap_manager* manager = malloc(sizeof(t_bitmap_manager));
    if (!manager) {
        log_error(logger, "Error al asignar memoria para bitmap manager");
        return NULL;
    }
    
    manager->num_blocks = num_blocks;
    manager->logger = logger;
    
    // Construir ruta del bitmap
    manager->bitmap_path = malloc(512);
    snprintf(manager->bitmap_path, 512, "%s/bitmap.bin", punto_montaje);
    
    // Calcular tamaño del bitmap en bytes
    uint32_t bitmap_size_bytes = (num_blocks + 7) / 8;
    
    // Leer el archivo bitmap.bin
    FILE* f = fopen(manager->bitmap_path, "rb");
    if (!f) {
        log_error(logger, "Error al abrir bitmap.bin: %s", strerror(errno));
        free(manager->bitmap_path);
        free(manager);
        return NULL;
    }
    
    char* bitmap_data = malloc(bitmap_size_bytes);
    if (!bitmap_data) {
        log_error(logger, "Error al asignar memoria para bitmap data");
        fclose(f);
        free(manager->bitmap_path);
        free(manager);
        return NULL;
    }
    
    fread(bitmap_data, 1, bitmap_size_bytes, f);
    fclose(f);
    
    // Crear bitarray con los datos leídos
    manager->bitarray = bitarray_create_with_mode(bitmap_data, bitmap_size_bytes, LSB_FIRST);
    pthread_mutex_init(&manager->mtx, NULL);
    
    log_info(logger, "Bitmap manager inicializado: %u bloques (%u bytes)", num_blocks, bitmap_size_bytes);
    
    return manager;
}

// Destruye el bitmap manager
void bitmap_manager_destroy(t_bitmap_manager* manager) {
    if (!manager) return;
    
    if (manager->bitarray) {
        // Sincronizar antes de destruir
        bitmap_sync_to_disk(manager);
        
        // Liberar el buffer interno del bitarray
        free(manager->bitarray->bitarray);
        bitarray_destroy(manager->bitarray);
    }
    pthread_mutex_destroy(&manager->mtx);
    
    free(manager->bitmap_path);
    free(manager);
}

// Encuentra el primer bloque libre
int32_t bitmap_find_free_block(t_bitmap_manager* manager) {
    pthread_mutex_lock(&manager->mtx);
    for (uint32_t i = 0; i < manager->num_blocks; i++) {
        if (!bitarray_test_bit(manager->bitarray, i)) {
            pthread_mutex_unlock(&manager->mtx);
            return (int32_t)i;
        }
    }
    pthread_mutex_unlock(&manager->mtx);
    
    log_warning(manager->logger, "No hay bloques libres disponibles");
    return -1;
}

// Marca un bloque como ocupado
bool bitmap_set_block_used(t_bitmap_manager* manager, uint32_t block_num) {
    if (block_num >= manager->num_blocks) {
        log_error(manager->logger, "Número de bloque inválido: %u", block_num);
        return false;
    }
    pthread_mutex_lock(&manager->mtx);
    bitarray_set_bit(manager->bitarray, block_num);
    pthread_mutex_unlock(&manager->mtx);
    log_trace(manager->logger, "Bloque %u marcado como ocupado", block_num);
    
    return true;
}

// Marca un bloque como libre
bool bitmap_set_block_free(t_bitmap_manager* manager, uint32_t block_num) {
    if (block_num >= manager->num_blocks) {
        log_error(manager->logger, "Número de bloque inválido: %u", block_num);
        return false;
    }
    pthread_mutex_lock(&manager->mtx);
    bitarray_clean_bit(manager->bitarray, block_num);
    pthread_mutex_unlock(&manager->mtx);
    log_trace(manager->logger, "Bloque %u marcado como libre", block_num);
    
    return true;
}

// Verifica si un bloque está ocupado
bool bitmap_is_block_used(t_bitmap_manager* manager, uint32_t block_num) {
    if (block_num >= manager->num_blocks) {
        return false;
    }
    pthread_mutex_lock(&manager->mtx);
    bool used = bitarray_test_bit(manager->bitarray, block_num);
    pthread_mutex_unlock(&manager->mtx);
    return used;
}

// Sincroniza el bitmap a disco
bool bitmap_sync_to_disk(t_bitmap_manager* manager) {
    FILE* f = fopen(manager->bitmap_path, "wb");
    if (!f) {
        log_error(manager->logger, "Error al abrir bitmap.bin para escritura: %s", strerror(errno));
        return false;
    }
    
    uint32_t bitmap_size_bytes = (manager->num_blocks + 7) / 8;
    pthread_mutex_lock(&manager->mtx);
    fwrite(manager->bitarray->bitarray, 1, bitmap_size_bytes, f);
    pthread_mutex_unlock(&manager->mtx);
    fclose(f);
    
    log_trace(manager->logger, "Bitmap sincronizado a disco");
    return true;
}

// Cuenta bloques libres
uint32_t bitmap_count_free_blocks(t_bitmap_manager* manager) {
    uint32_t free_count = 0;
    pthread_mutex_lock(&manager->mtx);
    for (uint32_t i = 0; i < manager->num_blocks; i++) {
        if (!bitarray_test_bit(manager->bitarray, i)) {
            free_count++;
        }
    }
    pthread_mutex_unlock(&manager->mtx);
    
    return free_count;
}

//Busca en el bitmap un bit en 0, lo pone en 1 y devuelve el número de bloque.
int reservar_bloque_libre(t_bitmap_manager* manager, uint32_t query_id) {
    if (!manager) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->mtx);
    // Buscar primer bloque libre (bit en 0)
    int32_t bloque_libre = -1;
    for (uint32_t i = 0; i < manager->num_blocks; i++) {
        if (!bitarray_test_bit(manager->bitarray, i)) {
            bloque_libre = (int32_t)i;
            break;
        }
    }
    if (bloque_libre == -1) {
        log_warning(manager->logger, "No hay bloques libres disponibles");
        pthread_mutex_unlock(&manager->mtx);
        return -1;
    }
    
    // Marcar como ocupado (bit en 1)
    bitarray_set_bit(manager->bitarray, (uint32_t)bloque_libre);
    pthread_mutex_unlock(&manager->mtx);

    // Sincronizar a disco
    if (!bitmap_sync_to_disk(manager)) {
        log_error(manager->logger, "Error al sincronizar bitmap después de reservar bloque %d", bloque_libre);
        // Revertir el cambio
        pthread_mutex_lock(&manager->mtx);
        bitarray_clean_bit(manager->bitarray, (uint32_t)bloque_libre);
        pthread_mutex_unlock(&manager->mtx);
        return -1;
    }

    // Log obligatorio
    log_info(manager->logger, "\x1b[32m##%u - Bloque Físico Reservado - Número de Bloque: %d\x1b[0m", query_id, bloque_libre);
    return bloque_libre;
}

//Pone el bit en 0 para liberar el bloque.
bool liberar_bloque(t_bitmap_manager* manager, uint32_t numero, uint32_t query_id) {
    if (!manager) {
        return false;
    }
    
    if (numero >= manager->num_blocks) {
        log_error(manager->logger, "Número de bloque inválido: %u (máximo: %u)", 
                 numero, manager->num_blocks - 1);
        return false;
    }
    
    // Verificar que el bloque esté ocupado
    pthread_mutex_lock(&manager->mtx);
    bool estaba_usado = bitarray_test_bit(manager->bitarray, numero);
    if (!estaba_usado) {
        pthread_mutex_unlock(&manager->mtx);
        log_warning(manager->logger, "Intento de liberar bloque ya libre: %u", numero);
        return false;
    }
    
    // Liberar bloque (bit en 0)
    bitarray_clean_bit(manager->bitarray, numero);
    pthread_mutex_unlock(&manager->mtx);
    
    // Sincronizar a disco
    if (!bitmap_sync_to_disk(manager)) {
        log_error(manager->logger, "Error al sincronizar bitmap después de liberar bloque %u", numero);
        return false;
    }
    
    // Log obligatorio
    log_info(manager->logger, "\x1b[32m##%u - Bloque Físico Liberado - Número de Bloque: %u\x1b[0m", query_id, numero);
    return true;
}

// Reconstruye el bitmap en base a los hardlinks en physical_blocks
bool bitmap_rebuild_from_physical_blocks(t_bitmap_manager* manager, const char* physical_blocks_dir) {
    if (!manager || !physical_blocks_dir) return false;

    pthread_mutex_lock(&manager->mtx);

    // poner todo en 0
    uint32_t bitmap_size_bytes = (manager->num_blocks + 7) / 8;
    memset(manager->bitarray->bitarray, 0, bitmap_size_bytes);

    char path[512];
    struct stat st;

    for (uint32_t i = 0; i < manager->num_blocks; i++) {
        snprintf(path, sizeof(path), "%s/block%04u.dat", physical_blocks_dir, i);
        if (stat(path, &st) == 0) {
            if (st.st_nlink > 1) {
                bitarray_set_bit(manager->bitarray, i);
            }
        }
    }

    pthread_mutex_unlock(&manager->mtx);

    return bitmap_sync_to_disk(manager);
}
