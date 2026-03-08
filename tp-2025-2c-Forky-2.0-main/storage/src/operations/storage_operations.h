#ifndef STORAGE_OPERATIONS_H
#define STORAGE_OPERATIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <commons/log.h>
#include "bitmap/bitmap_manager.h"
#include "hash_index/hash_index.h"
#include "file_metadata/file_metadata.h"

// Variables globales de retardos (definidas en main.c)
extern uint32_t RETARDO_OPERACION;
extern uint32_t RETARDO_ACCESO_BLOQUE;

/**
 * Contexto global del Storage con todas las estructuras necesarias.
 */
typedef struct {
    char* punto_montaje;
    uint32_t block_size;
    uint32_t num_blocks;
    t_bitmap_manager* bitmap;
    t_hash_index* hash_index;
    t_log* logger;
} t_storage_context;

/**
 * Inicializa el contexto del Storage.
 */
t_storage_context* storage_context_init(const char* punto_montaje, uint32_t block_size, 
                                       uint32_t num_blocks, t_log* logger);

/**
 * Destruye el contexto del Storage.
 */
void storage_context_destroy(t_storage_context* ctx);

/**
 * Operación: Crear un nuevo File con un Tag inicial.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag inicial
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_create_file(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id);

/**
 * Operación: Truncar (agrandar o achicar) un File:Tag.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param new_size Nuevo tamaño en bytes
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_truncate_file(t_storage_context* ctx, const char* file_name, 
                           const char* tag_name, uint32_t new_size, uint32_t query_id);

/**
 * Operación: Crear un nuevo Tag como copia de un Tag existente.
 * 
 * @param ctx Contexto del Storage
 * @param file_origen Nombre del File origen
 * @param tag_origen Nombre del Tag origen
 * @param file_destino Nombre del File destino
 * @param tag_destino Nombre del Tag destino
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_tag_file(t_storage_context* ctx, const char* file_origen,
                     const char* tag_origen, const char* file_destino, 
                     const char* tag_destino, uint32_t query_id);

/**
 * Operación: Confirmar (commit) un Tag.
 * Aplica deduplicación usando el hash index.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_commit_tag(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id);

/**
 * Operación: Escribir un bloque lógico de un File:Tag.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logical_block_num Número de bloque lógico
 * @param data Datos a escribir
 * @param size Tamaño de los datos
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_write_block(t_storage_context* ctx, const char* file_name, const char* tag_name,
                        uint32_t logical_block_num, const void* data, uint32_t size, uint32_t query_id);

/**
 * Operación: Leer un bloque lógico de un File:Tag.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logical_block_num Número de bloque lógico
 * @param buffer Buffer donde se escribirán los datos (debe tener BLOCK_SIZE bytes)
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_read_block(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t logical_block_num, void* buffer, uint32_t query_id);

/**
 * Operación: FLUSH Tag.
 * Sincroniza todos los buffers a disco.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File (opcional, NULL para flush global)
 * @param tag_name Nombre del Tag (opcional, NULL para flush global)
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_flush_tag(t_storage_context* ctx, const char* file_name, const char* tag_name);

/**
 * Operación: Eliminar un Tag.
 * Libera los bloques físicos que no sean referenciados por otros Tags.
 * 
 * @param ctx Contexto del Storage
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_delete_tag(t_storage_context* ctx, const char* file_name, const char* tag_name, uint32_t query_id);

/**
 * Operación: Escribir datos en un File:Tag (función de alto nivel).
 * 
 * @param ctx Contexto del Storage
 * @param file_tag Nombre completo en formato FILE:TAG
 * @param offset Offset en bytes donde escribir
 * @param data Datos a escribir
 * @param size Tamaño de los datos
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_write_file(t_storage_context* ctx, const char* file_tag, 
                       uint32_t offset, const void* data, uint32_t size);

/**
 * Operación: Leer datos de un File:Tag (función de alto nivel).
 * 
 * @param ctx Contexto del Storage
 * @param file_tag Nombre completo en formato FILE:TAG
 * @param offset Offset en bytes desde donde leer
 * @param size Tamaño de los datos a leer
 * @return Buffer con los datos leídos (debe liberarse con free), NULL si hay error
 */
void* storage_read_file(t_storage_context* ctx, const char* file_tag, 
                       uint32_t offset, uint32_t size);

/**
 * Operación: Eliminar un File:Tag completo (función de alto nivel).
 * 
 * @param ctx Contexto del Storage
 * @param file_tag Nombre completo en formato FILE:TAG
 * @return true si fue exitoso, false en caso contrario
 */
bool storage_delete_file(t_storage_context* ctx, const char* file_tag, uint32_t query_id);

// Variable global del contexto de Storage (definida en main.c)
extern t_storage_context* storage_ctx;

#endif // STORAGE_OPERATIONS_H
