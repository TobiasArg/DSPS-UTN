#include "fs_init.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <commons/config.h>
#include <commons/bitarray.h>
#include <commons/string.h>
#include <commons/crypto.h>

// Función auxiliar para eliminar recursivamente un directorio
static bool remove_directory_recursive(const char* path, t_log* logger) {
    DIR* dir = opendir(path);
    if (!dir) {
        return false;
    }

    struct dirent* entry;
    char filepath[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
        struct stat statbuf;
        if (stat(filepath, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                remove_directory_recursive(filepath, logger);
            } else {
                unlink(filepath);
            }
        }
    }
    
    closedir(dir);
    rmdir(path);
    return true;
}

// Función auxiliar para crear un directorio si no existe
static bool create_directory_if_not_exists(const char* path, t_log* logger) {
    struct stat st = {0};
    
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) == -1) {
            log_error(logger, "Error al crear directorio '%s': %s", path, strerror(errno));
            return false;
        }
        log_info(logger, "Directorio creado: %s", path);
    }
    
    return true;
}

// Crea el archivo bitmap.bin inicializado en ceros
static bool create_bitmap(const char* punto_montaje, uint32_t num_blocks, t_log* logger) {
    char bitmap_path[512];
    snprintf(bitmap_path, sizeof(bitmap_path), "%s/bitmap.bin", punto_montaje);
    
    // Calcular tamaño del bitmap en bytes
    uint32_t bitmap_size_bytes = (num_blocks + 7) / 8; // ceiling(num_blocks / 8)
    
    // Crear buffer inicializado en ceros (todos los bloques libres)
    char* bitmap_data = calloc(bitmap_size_bytes, 1);
    if (!bitmap_data) {
        log_error(logger, "Error al reservar memoria para bitmap");
        return false;
    }
    
    // Escribir el archivo
    FILE* f = fopen(bitmap_path, "wb");
    if (!f) {
        log_error(logger, "Error al crear bitmap.bin: %s", strerror(errno));
        free(bitmap_data);
        return false;
    }
    
    fwrite(bitmap_data, 1, bitmap_size_bytes, f);
    fclose(f);
    free(bitmap_data);
    
    log_info(logger, "Bitmap creado: %u bloques (%u bytes)", num_blocks, bitmap_size_bytes);
    return true;
}

// Crea el archivo blocks_hash_index.config vacío
static bool create_hash_index(const char* punto_montaje, t_log* logger) {
    char index_path[512];
    snprintf(index_path, sizeof(index_path), "%s/blocks_hash_index.config", punto_montaje);
    
    FILE* f = fopen(index_path, "w");
    if (!f) {
        log_error(logger, "Error al crear blocks_hash_index.config: %s", strerror(errno));
        return false;
    }
    
    // Archivo vacío inicialmente
    fclose(f);
    
    log_info(logger, "Hash index creado: %s", index_path);
    return true;
}

// Crea todos los bloques físicos del FS
static bool create_physical_blocks(const char* punto_montaje, uint32_t num_blocks, 
                                   uint32_t block_size, t_log* logger) {
    char blocks_dir[512];
    snprintf(blocks_dir, sizeof(blocks_dir), "%s/physical_blocks", punto_montaje);
    
    if (!create_directory_if_not_exists(blocks_dir, logger)) {
        return false;
    }
    
    // Crear buffer con ceros del tamaño de un bloque
    char* zero_block = calloc(block_size, 1);
    if (!zero_block) {
        log_error(logger, "Error al reservar memoria para bloque");
        return false;
    }
    
    // Crear cada archivo blockXXXX.dat
    for (uint32_t i = 0; i < num_blocks; i++) {
        char block_path[512];
        snprintf(block_path, sizeof(block_path), "%s/block%04u.dat", blocks_dir, i);
        
        FILE* f = fopen(block_path, "wb");
        if (!f) {
            log_error(logger, "Error al crear bloque %u: %s", i, strerror(errno));
            free(zero_block);
            return false;
        }
        
        fwrite(zero_block, 1, block_size, f);
        fclose(f);
    }
    
    free(zero_block);
    log_info(logger, "Bloques físicos creados: %u bloques de %u bytes", num_blocks, block_size);
    return true;
}

// Crea el directorio files/
static bool create_files_directory(const char* punto_montaje, t_log* logger) {
    char files_dir[512];
    snprintf(files_dir, sizeof(files_dir), "%s/files", punto_montaje);
    
    return create_directory_if_not_exists(files_dir, logger);
}

// Crea el File inicial: initial_file/BASE/
static bool create_initial_file(const char* punto_montaje, uint32_t block_size, t_log* logger) {
    char initial_file_path[512];
    char base_tag_path[512];
    char metadata_path[512];
    char logical_blocks_path[512];
    char logical_block_path[512];
    char physical_block_path[512];
    
    // 1. Llenar el bloque físico 0 con caracteres '0'
    snprintf(physical_block_path, sizeof(physical_block_path), "%s/physical_blocks/block0000.dat", punto_montaje);
    FILE* block0_file = fopen(physical_block_path, "wb");
    if (!block0_file) {
        log_error(logger, "Error al abrir bloque 0: %s", strerror(errno));
        return false;
    }
    
    // Llenar con caracteres '0' (ASCII 48)
    for (uint32_t i = 0; i < block_size; i++) {
        fputc('0', block0_file);
    }
    fclose(block0_file);
    
    log_info(logger, "Bloque 0 llenado con %u caracteres '0'", block_size);
    
    // 2. Calcular hash MD5 del bloque 0 y agregarlo al índice
    char* zero_block = malloc(block_size);
    memset(zero_block, '0', block_size);
    char* hash_block0 = string_from_format("%s", crypto_md5(zero_block, block_size));
    free(zero_block);
    
    // Agregar al blocks_hash_index.config
    char hash_index_path[512];
    snprintf(hash_index_path, sizeof(hash_index_path), "%s/blocks_hash_index.config", punto_montaje);
    FILE* hash_index = fopen(hash_index_path, "a");
    if (hash_index) {
        fprintf(hash_index, "%s=block0000\n", hash_block0);
        fclose(hash_index);
        log_info(logger, "Hash del bloque 0 agregado al índice: %s", hash_block0);
    }
    free(hash_block0);
    
    // 3. Crear directorio initial_file/
    snprintf(initial_file_path, sizeof(initial_file_path), "%s/files/initial_file", punto_montaje);
    if (!create_directory_if_not_exists(initial_file_path, logger)) {
        return false;
    }
    
    // 4. Crear subdirectorio BASE/
    snprintf(base_tag_path, sizeof(base_tag_path), "%s/BASE", initial_file_path);
    if (!create_directory_if_not_exists(base_tag_path, logger)) {
        return false;
    }
    
    // 5. Crear metadata.config
    snprintf(metadata_path, sizeof(metadata_path), "%s/metadata.config", base_tag_path);
    FILE* metadata = fopen(metadata_path, "w");
    if (!metadata) {
        log_error(logger, "Error al crear metadata.config de initial_file/BASE: %s", strerror(errno));
        return false;
    }
    
    fprintf(metadata, "TAMAÑO=%u\n", block_size);
    fprintf(metadata, "BLOCKS=[0]\n");
    fprintf(metadata, "ESTADO=COMMITED\n");
    fclose(metadata);
    
    // 6. Crear directorio logical_blocks/
    snprintf(logical_blocks_path, sizeof(logical_blocks_path), "%s/logical_blocks", base_tag_path);
    if (!create_directory_if_not_exists(logical_blocks_path, logger)) {
        return false;
    }
    
    // 7. Crear hard link al bloque físico 0
    snprintf(logical_block_path, sizeof(logical_block_path), "%s/000000.dat", logical_blocks_path);
    
    if (link(physical_block_path, logical_block_path) != 0) {
        log_error(logger, "Error al crear hard link para initial_file/BASE: %s", strerror(errno));
        return false;
    }
    
    log_info(logger, "initial_file/BASE creado correctamente (bloque 0, COMMITED, no borrable)");
    return true;
}

// Formatea el File System desde cero
bool fs_format(const char* punto_montaje, t_log* logger) {
    log_info(logger, "=== FORMATEANDO FILE SYSTEM ===");
    
    // 1. Leer superblock.config para obtener FS_SIZE y BLOCK_SIZE
    char superblock_path[512];
    snprintf(superblock_path, sizeof(superblock_path), "%s/superblock.config", punto_montaje);
    
    t_config* superblock = config_create(superblock_path);
    if (!superblock) {
        log_error(logger, "Error: No se encontró superblock.config en '%s'", punto_montaje);
        return false;
    }
    
    uint32_t fs_size = (uint32_t)config_get_int_value(superblock, "FS_SIZE");
    uint32_t block_size = (uint32_t)config_get_int_value(superblock, "BLOCK_SIZE");
    config_destroy(superblock);
    
    uint32_t num_blocks = fs_size / block_size;
    
    log_info(logger, "FS_SIZE=%u, BLOCK_SIZE=%u => %u bloques totales", 
             fs_size, block_size, num_blocks);
    
    // 2. Eliminar archivos y directorios previos (excepto superblock.config)
    char path[512];
    
    snprintf(path, sizeof(path), "%s/bitmap.bin", punto_montaje);
    unlink(path);
    
    snprintf(path, sizeof(path), "%s/blocks_hash_index.config", punto_montaje);
    unlink(path);
    
    snprintf(path, sizeof(path), "%s/physical_blocks", punto_montaje);
    remove_directory_recursive(path, logger);
    
    snprintf(path, sizeof(path), "%s/files", punto_montaje);
    remove_directory_recursive(path, logger);
    
    // Eliminar Files/ (mayúscula) si existe
    snprintf(path, sizeof(path), "%s/Files", punto_montaje);
    remove_directory_recursive(path, logger);
    
    log_info(logger, "Archivos previos eliminados");
    
    // 3. Crear bitmap.bin
    if (!create_bitmap(punto_montaje, num_blocks, logger)) {
        return false;
    }
    
    // 4. Crear blocks_hash_index.config
    if (!create_hash_index(punto_montaje, logger)) {
        return false;
    }
    
    // 5. Crear directorio physical_blocks/ y todos los bloques
    if (!create_physical_blocks(punto_montaje, num_blocks, block_size, logger)) {
        return false;
    }
    
    // 6. Crear directorio files/
    if (!create_files_directory(punto_montaje, logger)) {
        return false;
    }
    
    // 7. Crear initial_file/BASE/
    if (!create_initial_file(punto_montaje, block_size, logger)) {
        return false;
    }
    
    // 8. Marcar bloque 0 como ocupado en el bitmap
    snprintf(path, sizeof(path), "%s/bitmap.bin", punto_montaje);
    FILE* bitmap_file = fopen(path, "r+b");
    if (bitmap_file) {
        uint32_t bitmap_size = (num_blocks + 7) / 8;
        char* bitmap_data = malloc(bitmap_size);
        fread(bitmap_data, 1, bitmap_size, bitmap_file);
        
        t_bitarray* bitmap = bitarray_create_with_mode(bitmap_data, bitmap_size, LSB_FIRST);
        bitarray_set_bit(bitmap, 0); // Marcar bloque 0 como ocupado
        
        fseek(bitmap_file, 0, SEEK_SET);
        fwrite(bitmap_data, 1, bitmap_size, bitmap_file);
        fclose(bitmap_file);
        
        bitarray_destroy(bitmap);
        free(bitmap_data);
        
        log_info(logger, "Bloque 0 marcado como ocupado en bitmap");
    }
    
    log_info(logger, "=== FORMATEO COMPLETADO EXITOSAMENTE ===");
    return true;
}

// Carga las estructuras existentes del File System
bool fs_load_existing(const char* punto_montaje, t_log* logger) {
    log_info(logger, "=== CARGANDO FILE SYSTEM EXISTENTE ===");
    
    // Verificar que existan los archivos y directorios necesarios
    char path[512];
    struct stat st;
    
    snprintf(path, sizeof(path), "%s/superblock.config", punto_montaje);
    if (stat(path, &st) != 0) {
        log_error(logger, "Error: No existe superblock.config");
        return false;
    }
    
    snprintf(path, sizeof(path), "%s/bitmap.bin", punto_montaje);
    if (stat(path, &st) != 0) {
        log_error(logger, "Error: No existe bitmap.bin");
        return false;
    }
    
    snprintf(path, sizeof(path), "%s/blocks_hash_index.config", punto_montaje);
    if (stat(path, &st) != 0) {
        log_error(logger, "Error: No existe blocks_hash_index.config");
        return false;
    }
    
    snprintf(path, sizeof(path), "%s/physical_blocks", punto_montaje);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_error(logger, "Error: No existe directorio physical_blocks/");
        return false;
    }
    
    snprintf(path, sizeof(path), "%s/files", punto_montaje);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_error(logger, "Error: No existe directorio files/");
        return false;
    }
    
    log_info(logger, "Todas las estructuras del FS están presentes");
    log_info(logger, "=== FILE SYSTEM CARGADO EXITOSAMENTE ===");
    return true;
}

// Función principal de inicialización
bool fs_init(const char* punto_montaje, bool fresh_start, t_log* logger) {
    log_info(logger, "Inicializando File System en: %s", punto_montaje);
    log_info(logger, "FRESH_START=%s", fresh_start ? "TRUE" : "FALSE");
    
    if (fresh_start) {
        return fs_format(punto_montaje, logger);
    } else {
        return fs_load_existing(punto_montaje, logger);
    }
}
