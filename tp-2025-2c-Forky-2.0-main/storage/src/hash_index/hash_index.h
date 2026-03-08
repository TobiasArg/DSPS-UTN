#ifndef HASH_INDEX_H
#define HASH_INDEX_H

#include <stdint.h>
#include <stdbool.h>
#include <commons/log.h>
#include <pthread.h>
#include <commons/collections/dictionary.h>

/**
 * Estructura que maneja el índice hash del File System.
 * Asocia hashes MD5 con números de bloques físicos.
 */
typedef struct {
    t_dictionary* hash_to_block;  // hash -> "blockXXXX"
    char* index_path;
    t_log* logger;
    pthread_mutex_t mtx;           // Protege acceso concurrente al hash index
} t_hash_index;

/**
 * Inicializa el hash index cargando blocks_hash_index.config desde disco.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param logger Logger para mensajes
 * @return Puntero al hash index o NULL en caso de error
 */
t_hash_index* hash_index_init(const char* punto_montaje, t_log* logger);

/**
 * Destruye el hash index y libera recursos.
 * 
 * @param index Hash index a destruir
 */
void hash_index_destroy(t_hash_index* index);

/**
 * Busca un hash en el índice y devuelve el número de bloque físico asociado.
 * 
 * @param index Hash index
 * @param hash Hash MD5 a buscar (32 caracteres hex)
 * @return Número de bloque físico, o -1 si no se encontró
 */
int32_t hash_index_find_block(t_hash_index* index, const char* hash);

/**
 * Agrega una asociación hash -> bloque al índice.
 * 
 * @param index Hash index
 * @param hash Hash MD5 (32 caracteres hex)
 * @param block_num Número de bloque físico
 * @return true si fue exitoso, false en caso contrario
 */
bool hash_index_add_entry(t_hash_index* index, const char* hash, uint32_t block_num);

/**
 * Elimina una asociación hash -> bloque del índice.
 * 
 * @param index Hash index
 * @param hash Hash MD5 a eliminar
 * @return true si fue exitoso, false en caso contrario
 */
bool hash_index_remove_entry(t_hash_index* index, const char* hash);

/**
 * Sincroniza el hash index en memoria con blocks_hash_index.config en disco.
 * 
 * @param index Hash index
 * @return true si fue exitoso, false en caso contrario
 */
bool hash_index_sync_to_disk(t_hash_index* index);

/**
 * Cuenta cuántas veces un bloque físico está referenciado en el índice.
 * Útil para saber si un bloque puede ser liberado.
 * 
 * @param index Hash index
 * @param block_num Número de bloque físico
 * @return Cantidad de referencias al bloque
 */
uint32_t hash_index_count_block_references(t_hash_index* index, uint32_t block_num);

#endif // HASH_INDEX_H
