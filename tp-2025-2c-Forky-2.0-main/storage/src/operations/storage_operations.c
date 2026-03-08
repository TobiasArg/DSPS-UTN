#include "storage_operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <commons/string.h>
#include <commons/crypto.h>

// Definir _GNU_SOURCE para strndup si no está disponible
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Inicializa el contexto del Storage
t_storage_context* storage_context_init(const char* punto_montaje, uint32_t block_size, uint32_t num_blocks, t_log* logger) {
    t_storage_context* ctx = malloc(sizeof(t_storage_context));
    if (!ctx) return NULL;
    
    ctx->punto_montaje = strdup(punto_montaje);
    ctx->block_size = block_size;
    ctx->num_blocks = num_blocks;
    ctx->logger = logger;

    // Inicializar gestor global de locks FILE:TAG
    file_tag_lock_manager_init();
    
    // Inicializar bitmap
    log_info(logger, "Inicializando bitmap manager: %u bloques", num_blocks);
    ctx->bitmap = bitmap_manager_init(punto_montaje, num_blocks, logger);
    if (!ctx->bitmap) {
        free(ctx->punto_montaje);
        free(ctx);
        return NULL;
    }
    
    // Inicializar hash index
    log_info(logger, "Inicializando hash index en: %s", punto_montaje);
    ctx->hash_index = hash_index_init(punto_montaje, logger);
    if (!ctx->hash_index) {
        log_error(logger, "CRÍTICO: hash_index_init falló - Storage no puede inicializar");
        bitmap_manager_destroy(ctx->bitmap);
        free(ctx->punto_montaje);
        free(ctx);
        return NULL;
    }
    
    log_info(logger, "Hash index inicializado exitosamente");
    log_info(logger, "Contexto de Storage inicializado correctamente");
    return ctx;
}

// Destruye el contexto del Storage
void storage_context_destroy(t_storage_context* ctx) {
    if (!ctx) return;
    
    bitmap_manager_destroy(ctx->bitmap);
    hash_index_destroy(ctx->hash_index);
    file_tag_lock_manager_destroy();
    free(ctx->punto_montaje);
    free(ctx);
}

// CREATE: Crear un nuevo File con un Tag inicial
bool storage_create_file(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000); // convertir ms a microsegundos
    }

    log_info(ctx->logger, "CREATE: %s/%s", file_name, tag_name);
    
    // Verificar que el File no exista
    if (file_exists(ctx->punto_montaje, file_name)) {
        log_error(ctx->logger, "Error: File '%s' ya existe", file_name);
        return false;
    }
    
    // Crear el directorio del Tag (esto también crea el File si no existe)
    if (!create_tag_directory(ctx->punto_montaje, file_name, tag_name, ctx->logger)) {
        return false;
    }
    
    // Logs obligatorios
    log_info(ctx->logger, "\x1b[32m##%u - File Creado %s:%s\x1b[0m", query_id, file_name, tag_name);
    log_info(ctx->logger, "\x1b[33m##%u - Tag creado %s:%s\x1b[0m", query_id, file_name, tag_name);

    return true;
}

// TRUNCATE: Modificar el tamaño de un File:Tag
bool storage_truncate_file(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t new_size, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }

    log_info(ctx->logger, "TRUNCATE: %s/%s a %u bytes", file_name, tag_name, new_size);
    
    // LOCK FILE:TAG al inicio
    lock_file_tag(file_name, tag_name);
    
    // Verificar que el Tag exista
    if (!tag_exists(ctx->punto_montaje, file_name, tag_name)) {
        log_error(ctx->logger, "Error: File:Tag '%s/%s' no existe", file_name, tag_name);
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Leer metadata actual
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_name, tag_name, ctx->logger);
    if (!metadata) {
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Verificar que no esté COMMITED
    if (metadata->estado == ESTADO_COMMITED) {
        log_error(ctx->logger, "Error: No se puede truncar un Tag COMMITED");
        file_metadata_destroy(metadata);
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    uint32_t old_size = metadata->tamanio;
    uint32_t old_blocks = (old_size + ctx->block_size - 1) / ctx->block_size;
    uint32_t new_blocks = (new_size + ctx->block_size - 1) / ctx->block_size;
    
    if (new_blocks > old_blocks) {
        // AGRANDAR: Asignar nuevos bloques apuntando al bloque físico 0
        // El bloque 0 contiene el patrón de inicialización (zeros)
        // Copy-on-Write se activará en WRITE cuando se modifique
        log_info(ctx->logger, "Agrandando de %u a %u bloques", old_blocks, new_blocks);
        
        for (uint32_t i = old_blocks; i < new_blocks; i++) {
            // Apuntar al bloque físico 0 (sin reservar del bitmap)
            uint32_t physical_block = 0;

            // Crear hard link al bloque 0
            if (!create_logical_block_link(ctx->punto_montaje, file_name, tag_name, i, physical_block, ctx->logger, query_id)) {
                file_metadata_destroy(metadata);
                unlock_file_tag(file_name, tag_name);
                return false;
            }
            
            // Agregar bloque a la lista
            uint32_t* block_num = malloc(sizeof(uint32_t));
            *block_num = physical_block;
            list_add(metadata->blocks, block_num);

            log_debug(ctx->logger, "Bloque lógico %u -> bloque físico %u (hard link al bloque de inicialización)", i, physical_block);
        }
        
    } else if (new_blocks < old_blocks) {
        // ACHICAR: Eliminar bloques desde el final
        log_info(ctx->logger, "Achicando de %u a %u bloques", old_blocks, new_blocks);
        
        for (uint32_t i = old_blocks - 1; i >= new_blocks && i < old_blocks; i--) {
            // Obtener el bloque físico que se va a liberar
            uint32_t* block_num_ptr = list_remove(metadata->blocks, i);
            uint32_t physical_block = *block_num_ptr;
            free(block_num_ptr);
            
            // Eliminar hard link
            remove_logical_block_link(ctx->punto_montaje, file_name, tag_name, i, physical_block, ctx->logger, query_id);
            
            // Verificar si el bloque físico puede liberarse
            // Contar cuántos links tiene (usando stat)
            char physical_path[512];
            snprintf(physical_path, sizeof(physical_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, physical_block);
            
            struct stat st;
            if (stat(physical_path, &st) == 0) {
                // st_nlink es la cantidad de hard links
                if (st.st_nlink == 1) {
                    // Solo queda un link (el archivo físico original), liberar
                    if (liberar_bloque(ctx->bitmap, physical_block, query_id)) {
                        log_info(ctx->logger, "Bloque físico %u liberado (sin referencias)", physical_block);
                    } else {
                        log_warning(ctx->logger, "Error al liberar bloque físico %u", physical_block);
                    }
                } else {
                    log_debug(ctx->logger, "Bloque físico %u sigue referenciado (%ld links)", physical_block, st.st_nlink - 1);
                }
            }
        }
    }
    
    // Actualizar metadata
    metadata->tamanio = new_size;
    write_metadata(metadata, ctx->logger);
    
    // Log obligatorio  
    log_info(ctx->logger, "\x1b[32m##%u - File Truncado %s:%s - Tamaño: %u\x1b[0m", query_id, file_name, tag_name, new_size);

    // Rebuild bitmap para reflejar uso real
    char physical_dir[512];
    snprintf(physical_dir, sizeof(physical_dir), "%s/physical_blocks", ctx->punto_montaje);
    bitmap_rebuild_from_physical_blocks(ctx->bitmap, physical_dir);
    
    file_metadata_destroy(metadata);
    
    // UNLOCK FILE:TAG al final
    unlock_file_tag(file_name, tag_name);
    return true;
}

// Función auxiliar para copiar directorio recursivamente
static bool copy_directory_recursive(const char* src, const char* dst, t_log* logger) {
    // Crear directorio destino
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
        log_error(logger, "Error al crear directorio '%s': %s", dst, strerror(errno));
        return false;
    }
    
    DIR* dir = opendir(src);
    if (!dir) {
        log_error(logger, "Error al abrir directorio '%s': %s", src, strerror(errno));
        return false;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char src_path[1024], dst_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        
        struct stat st;
        if (stat(src_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Recursión para subdirectorios
                if (!copy_directory_recursive(src_path, dst_path, logger)) {
                    closedir(dir);
                    return false;
                }
            } else {
                // metadata.config debe copiarse como archivo nuevo (NO hardlink)
                if (strcmp(entry->d_name, "metadata.config") == 0) {
                    FILE* src_file = fopen(src_path, "rb");
                    if (!src_file) {
                        log_error(logger, "Error al abrir metadata origen '%s': %s", src_path, strerror(errno));
                        closedir(dir);
                        return false;
                    }
                    FILE* dst_file = fopen(dst_path, "wb");
                    if (!dst_file) {
                        log_error(logger, "Error al crear metadata destino '%s': %s", dst_path, strerror(errno));
                        fclose(src_file);
                        closedir(dir);
                        return false;
                    }
                    // Copiar contenido
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    fclose(src_file);
                    fclose(dst_file);
                } else {
                    // Para otros archivos (logical_blocks/*.dat), hacer hardlink
                    if (link(src_path, dst_path) != 0) {
                        log_error(logger, "Error al copiar '%s' a '%s': %s", src_path, dst_path, strerror(errno));
                        closedir(dir);
                        return false;
                    }
                }
            }
        }
    }
    
    closedir(dir);
    return true;
}

// TAG: Crear copia de un Tag
bool storage_tag_file(t_storage_context* ctx, const char* file_origen,const char* tag_origen, const char* file_destino, const char* tag_destino, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }
    
    log_info(ctx->logger, "TAG: %s/%s -> %s/%s", file_origen, tag_origen, file_destino, tag_destino);

    // LOCK FILE:TAG origen para proteger durante la copia de links
    lock_file_tag(file_origen, tag_origen);
    
    // Verificar que el Tag origen exista
    if (!tag_exists(ctx->punto_montaje, file_origen, tag_origen)) {
        log_error(ctx->logger, "Error: Tag origen '%s/%s' no existe", file_origen, tag_origen);
        unlock_file_tag(file_origen, tag_origen);
        return false;
    }
    
    // Verificar que el Tag destino no exista
    if (tag_exists(ctx->punto_montaje, file_destino, tag_destino)) {
        log_error(ctx->logger, "Error: Tag destino '%s/%s' ya existe", file_destino, tag_destino);
        unlock_file_tag(file_origen, tag_origen);
        return false;
    }
    
    // Copiar directorio completo desde archivo origen al destino
    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s/files/%s/%s", ctx->punto_montaje, file_origen, tag_origen);
    snprintf(dst_path, sizeof(dst_path), "%s/files/%s/%s", ctx->punto_montaje, file_destino, tag_destino);
    
    // Crear directorio del archivo destino si no existe
    char file_dst_path[512];
    snprintf(file_dst_path, sizeof(file_dst_path), "%s/files/%s", ctx->punto_montaje, file_destino);
    if (mkdir(file_dst_path, 0755) == -1 && errno != EEXIST) {
        log_error(ctx->logger, "Error creando directorio del archivo destino: %s", strerror(errno));
        unlock_file_tag(file_origen, tag_origen);
        return false;
    }
    
    if (!copy_directory_recursive(src_path, dst_path, ctx->logger)) {
        unlock_file_tag(file_origen, tag_origen);
        return false;
    }
    
    // Modificar metadata del Tag destino a WORK_IN_PROGRESS
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_destino, tag_destino, ctx->logger);
    if (metadata) {
        metadata->estado = ESTADO_WORK_IN_PROGRESS;
        write_metadata(metadata, ctx->logger);
        file_metadata_destroy(metadata);
    }
    
    // Log obligatorio
    log_info(ctx->logger, "\x1b[33m##%u - Tag creado %s:%s\x1b[0m", query_id, file_destino, tag_destino);

    // Rebuild bitmap tras duplicar hardlinks
    char physical_dir[512];
    snprintf(physical_dir, sizeof(physical_dir), "%s/physical_blocks", ctx->punto_montaje);
    bitmap_rebuild_from_physical_blocks(ctx->bitmap, physical_dir);

    unlock_file_tag(file_origen, tag_origen);
    
    return true;
}

// COMMIT: Confirmar un Tag con deduplicación
bool storage_commit_tag(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }
    
    log_info(ctx->logger, "COMMIT: %s/%s", file_name, tag_name);
    
    // LOCK FILE:TAG al inicio
    lock_file_tag(file_name, tag_name);
    
    // Leer metadata
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_name, tag_name, ctx->logger);
    if (!metadata) {
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Si ya está COMMITED, no hacer nada
    if (metadata->estado == ESTADO_COMMITED) {
        log_info(ctx->logger, "Tag %s/%s ya está COMMITED", file_name, tag_name);
        file_metadata_destroy(metadata);
        unlock_file_tag(file_name, tag_name);
        return true;
    }
    
    // Para cada bloque lógico, aplicar deduplicación
    int num_blocks = list_size(metadata->blocks);
    int bloques_deduplicados = 0;
    int bloques_nuevos = 0;
    
    for (int i = 0; i < num_blocks; i++) {
        uint32_t* physical_block_ptr = list_get(metadata->blocks, i);
        uint32_t current_physical_block = *physical_block_ptr;
        
        // Leer contenido del bloque físico
        char block_path[512];
        snprintf(block_path, sizeof(block_path), "%s/physical_blocks/block%04u.dat",ctx->punto_montaje, current_physical_block);
        
        FILE* f = fopen(block_path, "rb");
        if (!f) continue;
        
        char* block_data = malloc(ctx->block_size);
        fread(block_data, 1, ctx->block_size, f);
        fclose(f);
        
        // Calcular hash MD5
        char* hash = string_from_format("%s", crypto_md5(block_data, ctx->block_size));
        
        // Buscar en el índice si existe un bloque con el mismo hash
        // CRÍTICO: Proteger acceso al hash_index con mutex global
        pthread_mutex_lock(&ctx->hash_index->mtx);
        int32_t existing_block = hash_index_find_block(ctx->hash_index, hash);
        
        if (existing_block >= 0 && (uint32_t)existing_block != current_physical_block) {
            // Deduplicación: reapuntar al bloque existente
            // Log obligatorio de deduplicación
            log_info(ctx->logger, "\x1b[32m##%u - %s:%s Bloque Lógico %d se reasigna de %u a %u\x1b[0m", query_id, file_name, tag_name, i, current_physical_block, existing_block);
            
            bloques_deduplicados++;

            // Eliminar hard link actual
            remove_logical_block_link(ctx->punto_montaje, file_name, tag_name, i, current_physical_block, ctx->logger, query_id);
            
            // Crear nuevo hard link al bloque existente
            create_logical_block_link(ctx->punto_montaje, file_name, tag_name, i, existing_block, ctx->logger, query_id);
            
            // Actualizar metadata
            *physical_block_ptr = (uint32_t)existing_block;
            
            // Verificar si el bloque anterior puede liberarse
            char old_block_path[512];
            snprintf(old_block_path, sizeof(old_block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, current_physical_block);
            
            struct stat st;
            if (stat(old_block_path, &st) == 0 && st.st_nlink == 1) {
                // Liberar bloque duplicado
                if (liberar_bloque(ctx->bitmap, current_physical_block, query_id)) {
                    log_info(ctx->logger, "Bloque físico %u liberado tras deduplicación", current_physical_block);
                } else {
                    log_warning(ctx->logger, "Error al liberar bloque físico %u tras deduplicación", current_physical_block);
                }
            }
            
        } else if (existing_block < 0) {
            // No existe, agregar al índice
            hash_index_add_entry(ctx->hash_index, hash, current_physical_block);
            log_trace(ctx->logger, "Hash %s agregado al índice (bloque %u)", hash, current_physical_block);
            bloques_nuevos++;
        }
        
        // Liberar mutex del hash_index
        pthread_mutex_unlock(&ctx->hash_index->mtx);
        
        free(hash);
        free(block_data);
    }
    
    // Cambiar estado a COMMITED
    metadata->estado = ESTADO_COMMITED;
    write_metadata(metadata, ctx->logger);
    
    // Sincronizar estructuras
    bitmap_sync_to_disk(ctx->bitmap);
    hash_index_sync_to_disk(ctx->hash_index);
    
    // Log obligatorio
    log_info(ctx->logger, "\x1b[33m##%u - Commit de File:Tag %s:%s\x1b[0m", query_id, file_name, tag_name);
    
    // Mostrar estadísticas de deduplicación
    log_info(ctx->logger, "Estadísticas: %d bloques deduplicados, %d bloques nuevos de %d totales", bloques_deduplicados, bloques_nuevos, num_blocks);
    
    if (bloques_deduplicados > 0) {
        float porcentaje = (float)bloques_deduplicados / num_blocks * 100.0f;
        log_info(ctx->logger, "Deduplicación: %.1f%% de espacio ahorrado", porcentaje);
    }

    file_metadata_destroy(metadata);

    // Rebuild bitmap tras deduplicación/commit
    char physical_dir[512];
    snprintf(physical_dir, sizeof(physical_dir), "%s/physical_blocks", ctx->punto_montaje);
    bitmap_rebuild_from_physical_blocks(ctx->bitmap, physical_dir);
    
    // UNLOCK FILE:TAG al final
    unlock_file_tag(file_name, tag_name);
    return true;
}

// FLUSH: Sincronizar buffers a disco
bool storage_flush_tag(t_storage_context* ctx, const char* file_name, const char* tag_name) {
    log_info(ctx->logger, "FLUSH: %s/%s", file_name ? file_name : "GLOBAL", tag_name ? tag_name : "ALL");
    
    // FLUSH sincroniza todas las estructuras críticas a disco
    bool success = true;
    
    // 1. Sincronizar bitmap a disco (FASE 2)
    if (!bitmap_sync_to_disk(ctx->bitmap)) {
        log_error(ctx->logger, "Error sincronizando bitmap a disco");
        success = false;
    }
    
    // 2. Sincronizar hash index a disco (FASE 4)
    if (!hash_index_sync_to_disk(ctx->hash_index)) {
        log_error(ctx->logger, "Error sincronizando hash index a disco");
        success = false;
    }
    
    // 3. Llamar a sync() del sistema para forzar escritura a disco
    sync();
    
    if (success) {
        log_info(ctx->logger, "FLUSH exitoso: Todas las estructuras sincronizadas a disco");
    } else {
        log_error(ctx->logger, "FLUSH falló: Error en sincronización");
    }
    
    return success;
}

// WRITE: Escribir un bloque lógico
bool storage_write_block(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t logical_block_num, const void* data, uint32_t size, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }
    
    log_info(ctx->logger, "WRITE: %s/%s bloque lógico %u", file_name, tag_name, logical_block_num);
    
    // Leer metadata
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_name, tag_name, ctx->logger);
    if (!metadata) {
        return false;
    }
    
    // Verificar que no esté COMMITED
    if (metadata->estado == ESTADO_COMMITED) {
        log_error(ctx->logger, "Error: No se puede escribir en un Tag COMMITED");
        file_metadata_destroy(metadata);
        return false;
    }
    
    // Verificar que el bloque lógico esté asignado
    if (logical_block_num >= (uint32_t)list_size(metadata->blocks)) {
        log_error(ctx->logger, "Error: Bloque lógico %u fuera de límite", logical_block_num);
        file_metadata_destroy(metadata);
        return false;
    }
    
    uint32_t* physical_block_ptr = list_get(metadata->blocks, logical_block_num);
    uint32_t current_physical_block = *physical_block_ptr;
    
    // Verificar si el bloque físico tiene más de un hard link
    char block_path[512];
    snprintf(block_path, sizeof(block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, current_physical_block);
    
    struct stat st;
    if (stat(block_path, &st) != 0) {
        log_error(ctx->logger, "Error: No se puede acceder al bloque físico %u", current_physical_block);
        file_metadata_destroy(metadata);
        return false;
    }
    
    uint32_t target_physical_block = current_physical_block;
    
    // Copy-on-Write: si el bloque tiene múltiples referencias o es el bloque 0 (read-only)
    if (st.st_nlink > 1 || current_physical_block == 0) {
        // Hay múltiples referencias o es el bloque de inicialización, necesitamos un nuevo bloque físico (Copy-on-Write)

        int32_t new_block = reservar_bloque_libre(ctx->bitmap, query_id);
        if (new_block < 0) {
            log_error(ctx->logger, "Error: No hay bloques libres disponibles para Copy-on-Write");
            file_metadata_destroy(metadata);
            return false;
        }
        
        target_physical_block = (uint32_t)new_block;
        
        // Crear el archivo del nuevo bloque físico copiando el contenido original
        char new_block_path[512];
        snprintf(new_block_path, sizeof(new_block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, target_physical_block);
        
        // Copiar contenido del bloque original al nuevo
        FILE* src = fopen(block_path, "rb");
        FILE* dst = fopen(new_block_path, "wb");
        if (!src || !dst) {
            log_error(ctx->logger, "Error al crear nuevo bloque físico para CoW: %s", strerror(errno));
            if (src) fclose(src);
            if (dst) fclose(dst);
            liberar_bloque(ctx->bitmap, target_physical_block, query_id);
            file_metadata_destroy(metadata);
            return false;
        }
        
        // Copiar datos
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes_read, dst);
        }
        
        fclose(src);
        fclose(dst);

        // Eliminar hard link anterior
        remove_logical_block_link(ctx->punto_montaje, file_name, tag_name, logical_block_num, current_physical_block, ctx->logger, query_id);
        
        // Crear nuevo hard link
        if (!create_logical_block_link(ctx->punto_montaje, file_name, tag_name, logical_block_num, target_physical_block, ctx->logger, query_id)) {
            log_error(ctx->logger, "Error al crear hard link para nuevo bloque");
            liberar_bloque(ctx->bitmap, target_physical_block, query_id);
            file_metadata_destroy(metadata);
            return false;
        }
        
        // Actualizar metadata
        *physical_block_ptr = target_physical_block;
        if (!write_metadata(metadata, ctx->logger)) {
            log_error(ctx->logger, "Error al actualizar metadata después de CoW");
            file_metadata_destroy(metadata);
            return false;
        }
        
        log_info(ctx->logger, "CoW aplicado: nuevo bloque físico %u asignado", target_physical_block);
    }
    
    // Escribir datos en el bloque físico
    snprintf(block_path, sizeof(block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, target_physical_block);
    
    FILE* f = fopen(block_path, "wb");
    if (!f) {
        log_error(ctx->logger, "Error al abrir bloque físico para escritura: %s", strerror(errno));
        file_metadata_destroy(metadata);
        return false;
    }

    // Aplicar retardo de acceso a bloque
    if (RETARDO_ACCESO_BLOQUE > 0) {
        usleep(RETARDO_ACCESO_BLOQUE * 1000);
    }
    
    // Debug: verificar contenido del buffer
    char preview[64] = {0};
    uint32_t preview_len = size < 32 ? size : 32;
    for (uint32_t i = 0; i < preview_len; i++) {
        unsigned char c = ((unsigned char*)data)[i];
        if (c >= 32 && c <= 126) {
            strncat(preview, (char*)&c, 1);
        } else {
            strcat(preview, ".");
        }
    }
    log_debug(ctx->logger, "[WRITE_BLOCK] Escribiendo bloque %u con %u bytes: \"%s\"", 
              logical_block_num, size, preview);
    
    // Escribir hasta BLOCK_SIZE bytes (rellenar con ceros si es necesario)
    size_t bytes_written = fwrite(data, 1, size < ctx->block_size ? size : ctx->block_size, f);
    
    if (bytes_written != (size < ctx->block_size ? size : ctx->block_size)) {
        log_error(ctx->logger, "[WRITE_BLOCK] Error: solo se escribieron %zu de %u bytes", 
                  bytes_written, size < ctx->block_size ? size : ctx->block_size);
    }
    
    // Rellenar con ceros si size < BLOCK_SIZE
    if (size < ctx->block_size) {
        char zero = 0;
        for (uint32_t i = size; i < ctx->block_size; i++) {
            fwrite(&zero, 1, 1, f);
        }
    }
    
    fclose(f);
    
     // Log obligatorio
    log_info(ctx->logger, "\x1b[36m##%u - Bloque Lógico Escrito %s:%s - Número de Bloque: %u\x1b[0m", query_id, file_name, tag_name, logical_block_num);

    file_metadata_destroy(metadata);
    return true;
}

// READ: Leer un bloque lógico
bool storage_read_block(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t logical_block_num, void* buffer, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }
    
    log_info(ctx->logger, "READ: %s/%s bloque lógico %u", file_name, tag_name, logical_block_num);
    
    // Leer metadata
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_name, tag_name, ctx->logger);
    if (!metadata) {
        return false;
    }
    
    // Verificar que el bloque lógico esté asignado
    if (logical_block_num >= (uint32_t)list_size(metadata->blocks)) {
        log_error(ctx->logger, "Error: Bloque lógico %u fuera de límite", logical_block_num);
        file_metadata_destroy(metadata);
        return false;
    }
    
    uint32_t* physical_block_ptr = list_get(metadata->blocks, logical_block_num);
    uint32_t physical_block = *physical_block_ptr;
    
    // Leer del bloque físico
    char block_path[512];
    snprintf(block_path, sizeof(block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, physical_block);
    
    FILE* f = fopen(block_path, "rb");
    if (!f) {
        log_error(ctx->logger, "Error al abrir bloque físico %u para lectura: %s", physical_block, strerror(errno));
        file_metadata_destroy(metadata);
        return false;
    }

    // Aplicar retardo de acceso a bloque
    if (RETARDO_ACCESO_BLOQUE > 0) {
        usleep(RETARDO_ACCESO_BLOQUE * 1000);
    }
    
    fread(buffer, 1, ctx->block_size, f);
    fclose(f);
    
    // Log obligatorio
    log_info(ctx->logger, "\x1b[36m##%u - Bloque Lógico Leído %s:%s - Número de Bloque: %u\x1b[0m", query_id, file_name, tag_name, logical_block_num);
    
    file_metadata_destroy(metadata);
    return true;
}

// DELETE: Eliminar un Tag
bool storage_delete_tag(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id) {
    // Aplicar retardo de operación
    if (RETARDO_OPERACION > 0) {
        usleep(RETARDO_OPERACION * 1000);
    }
    
    log_info(ctx->logger, "DELETE: %s/%s", file_name, tag_name);

    // LOCK FILE:TAG al inicio
    lock_file_tag(file_name, tag_name);
    
    // Verificar que no sea initial_file/BASE (protegido)
    if (strcmp(file_name, "initial_file") == 0 && strcmp(tag_name, "BASE") == 0) {
        log_error(ctx->logger, "Error: No se puede eliminar initial_file/BASE (protegido)");
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Verificar que el Tag exista
    if (!tag_exists(ctx->punto_montaje, file_name, tag_name)) {
        log_error(ctx->logger, "Error: Tag '%s/%s' no existe", file_name, tag_name);
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Leer metadata para obtener los bloques
    t_file_tag_metadata* metadata = read_metadata(ctx->punto_montaje, file_name, tag_name, ctx->logger);
    if (!metadata) {
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Para cada bloque, verificar si puede liberarse
    int num_blocks = list_size(metadata->blocks);
    
    for (int i = 0; i < num_blocks; i++) {
        uint32_t* physical_block_ptr = list_get(metadata->blocks, i);
        uint32_t physical_block = *physical_block_ptr;
        
        // Eliminar hard link
        remove_logical_block_link(ctx->punto_montaje, file_name, tag_name, i, physical_block, ctx->logger, query_id);
        
        // Verificar referencias restantes
        char block_path[512];
        snprintf(block_path, sizeof(block_path), "%s/physical_blocks/block%04u.dat", ctx->punto_montaje, physical_block);
        
        struct stat st;
        if (stat(block_path, &st) == 0 && st.st_nlink == 1) {
            // Solo queda el archivo físico original, liberar
            if (liberar_bloque(ctx->bitmap, physical_block, query_id)) {
                log_info(ctx->logger, "Bloque físico %u liberado (sin referencias)", physical_block);
            } else {
                log_warning(ctx->logger, "Error al liberar bloque físico %u", physical_block);
            }
        }
    }
    
    file_metadata_destroy(metadata);
    
    // Eliminar directorio del Tag
    char tag_path[512];
    snprintf(tag_path, sizeof(tag_path), "%s/files/%s/%s", ctx->punto_montaje, file_name, tag_name);
    
    // Eliminar directorio recursivamente (usando system() por simplicidad)
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tag_path);
    if (system(cmd) != 0) {
        log_error(ctx->logger, "Error al eliminar directorio %s", tag_path);
        unlock_file_tag(file_name, tag_name);
        return false;
    }
    
    // Sincronizar bitmap
    bitmap_sync_to_disk(ctx->bitmap);

    // Rebuild bitmap tras eliminar links
    char physical_dir[512];
    snprintf(physical_dir, sizeof(physical_dir), "%s/physical_blocks", ctx->punto_montaje);
    bitmap_rebuild_from_physical_blocks(ctx->bitmap, physical_dir);
    
    // Log obligatorio
    log_info(ctx->logger, "\x1b[32m##%u - Tag Eliminado %s:%s\x1b[0m", query_id, file_name, tag_name);

    // UNLOCK FILE:TAG al final
    unlock_file_tag(file_name, tag_name);

    return true;
}

// ===== FUNCIONES DE ALTO NIVEL =====

/**
 * Función auxiliar para parsear FILE:TAG en file_name y tag_name separados
 */
static bool parse_file_tag(const char* file_tag, char** file_name, char** tag_name) {
    if (!file_tag) return false;
    
    char* colon = strchr(file_tag, ':');
    if (!colon) return false;
    
    size_t file_len = colon - file_tag;
    *file_name = strndup(file_tag, file_len);
    *tag_name = strdup(colon + 1);
    
    return (*file_name != NULL && *tag_name != NULL);
}

// Escribir datos en un File:Tag (función de alto nivel)
bool storage_write_file(t_storage_context* ctx, const char* file_tag, uint32_t offset, const void* data, uint32_t size) {
    if (!ctx || !file_tag || !data || size == 0) {
        return false;
    }
    
    char* file_name = NULL;
    char* tag_name = NULL;
    
    if (!parse_file_tag(file_tag, &file_name, &tag_name)) {
        log_error(ctx->logger, "Formato inválido en WRITE: %s. Esperado FILE:TAG", file_tag);
        return false;
    }
    
    log_info(ctx->logger, "##WRITE '%s:%s' offset=%u size=%u", file_name, tag_name, offset, size);
    
    // LOCK FILE:TAG al inicio
    lock_file_tag(file_name, tag_name);
    
    // Calcular bloques afectados
    uint32_t start_block = offset / ctx->block_size;
    uint32_t end_block = (offset + size - 1) / ctx->block_size;
    
    bool success = true;
    uint32_t data_offset = 0;
    
    for (uint32_t block = start_block; block <= end_block && success; block++) {
        // Calcular offset dentro del bloque y tamaño a escribir
        uint32_t block_offset = (block == start_block) ? (offset % ctx->block_size) : 0;
        uint32_t bytes_in_block = ctx->block_size - block_offset;
        uint32_t remaining_bytes = size - data_offset;
        uint32_t write_size = (bytes_in_block < remaining_bytes) ? bytes_in_block : remaining_bytes;
        
        // Si no escribimos el bloque completo, primero leemos el contenido existente
        char block_data[ctx->block_size];
        memset(block_data, 0, ctx->block_size);
        
        if (block_offset > 0 || write_size < ctx->block_size) {
            storage_read_block(ctx, file_name, tag_name, block, block_data, 0);
        }
        
        // Copiar los nuevos datos
        memcpy(block_data + block_offset, (char*)data + data_offset, write_size);
        
        // Escribir el bloque completo
        success = storage_write_block(ctx, file_name, tag_name, block, block_data, ctx->block_size, 0);
        data_offset += write_size;
    }
    
    free(file_name);
    free(tag_name);
    
    // UNLOCK FILE:TAG al final
    unlock_file_tag(file_name, tag_name);
    return success;
}

// Leer datos de un File:Tag (función de alto nivel)
void* storage_read_file(t_storage_context* ctx, const char* file_tag, uint32_t offset, uint32_t size) {
    if (!ctx || !file_tag || size == 0) {
        return NULL;
    }
    
    char* file_name = NULL;
    char* tag_name = NULL;
    
    if (!parse_file_tag(file_tag, &file_name, &tag_name)) {
        log_error(ctx->logger, "Formato inválido en READ: %s. Esperado FILE:TAG", file_tag);
        return NULL;
    }
    
    log_info(ctx->logger, "##READ '%s:%s' offset=%u size=%u", file_name, tag_name, offset, size);
    
    // Allocar buffer para el resultado
    void* result = malloc(size);
    if (!result) {
        free(file_name);
        free(tag_name);
        return NULL;
    }
    
    // Calcular bloques a leer
    uint32_t start_block = offset / ctx->block_size;
    uint32_t end_block = (offset + size - 1) / ctx->block_size;
    
    uint32_t data_offset = 0;
    bool success = true;
    
    for (uint32_t block = start_block; block <= end_block && success; block++) {
        char block_data[ctx->block_size];
        
        if (!storage_read_block(ctx, file_name, tag_name, block, block_data, 0)) {
            success = false;
            break;
        }
        
        // Calcular qué parte del bloque copiar
        uint32_t block_offset = (block == start_block) ? (offset % ctx->block_size) : 0;
        uint32_t bytes_in_block = ctx->block_size - block_offset;
        uint32_t remaining_bytes = size - data_offset;
        uint32_t copy_size = (bytes_in_block < remaining_bytes) ? bytes_in_block : remaining_bytes;
        
        memcpy((char*)result + data_offset, block_data + block_offset, copy_size);
        data_offset += copy_size;
    }
    
    free(file_name);
    free(tag_name);
    
    if (!success) {
        free(result);
        return NULL;
    }
    
    return result;
}

// Eliminar un File:Tag completo (función de alto nivel)
bool storage_delete_file(t_storage_context* ctx, const char* file_tag, uint32_t query_id) {
    if (!ctx || !file_tag) {
        return false;
    }
    
    char* file_name = NULL;
    char* tag_name = NULL;
    
    if (!parse_file_tag(file_tag, &file_name, &tag_name)) {
        log_error(ctx->logger, "Formato inválido en DELETE: %s. Esperado FILE:TAG", file_tag);
        return false;
    }
    
    log_info(ctx->logger, "##DELETE '%s:%s'", file_name, tag_name);
    
    bool success = storage_delete_tag(ctx, file_name, tag_name, 0);

    free(file_name);
    free(tag_name);
    
    return success;
}