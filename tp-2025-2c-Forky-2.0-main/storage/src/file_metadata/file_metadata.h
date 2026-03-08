#ifndef FILE_METADATA_H
#define FILE_METADATA_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <commons/collections/dictionary.h>

// Estados posibles de un File:Tag
typedef enum {
    ESTADO_WORK_IN_PROGRESS,
    ESTADO_COMMITED
} t_file_tag_estado;

/**
 * Estructura que representa el metadata de un File:Tag.
 */
typedef struct {
    uint32_t tamanio;              // Tamaño en bytes
    t_list* blocks;                // Lista de números de bloques físicos
    t_file_tag_estado estado;      // WORK_IN_PROGRESS o COMMITED
    char* file_name;               // Nombre del File
    char* tag_name;                // Nombre del Tag
    char* metadata_path;           // Ruta al metadata.config
} t_file_tag_metadata;

/**
 * Crea el directorio para un nuevo File en el FS.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param logger Logger para mensajes
 * @return true si fue exitoso, false en caso contrario
 */
bool create_file_directory(const char* punto_montaje, const char* file_name, t_log* logger);

/**
 * Crea el directorio para un nuevo Tag dentro de un File.
 * También crea el metadata.config y el directorio logical_blocks/.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logger Logger para mensajes
 * @return true si fue exitoso, false en caso contrario
 */
bool create_tag_directory(const char* punto_montaje, const char* file_name, 
                          const char* tag_name, t_log* logger);

/**
 * Lee el metadata.config de un File:Tag.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logger Logger para mensajes
 * @return Puntero al metadata o NULL en caso de error
 */
t_file_tag_metadata* read_metadata(const char* punto_montaje, const char* file_name, 
                                   const char* tag_name, t_log* logger);

/**
 * Escribe el metadata.config de un File:Tag al disco.
 * 
 * @param metadata Metadata a escribir
 * @param logger Logger para mensajes
 * @return true si fue exitoso, false en caso contrario
 */
bool write_metadata(t_file_tag_metadata* metadata, t_log* logger);

/**
 * Destruye el metadata y libera memoria.
 * 
 * @param metadata Metadata a destruir
 */
void file_metadata_destroy(t_file_tag_metadata* metadata);

/**
 * Crea un hard link para un bloque lógico apuntando a un bloque físico.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logical_block_num Número de bloque lógico
 * @param physical_block_num Número de bloque físico
 * @param logger Logger para mensajes
 * @return true si fue exitoso, false en caso contrario
 */
bool create_logical_block_link(const char* punto_montaje, const char* file_name,
                               const char* tag_name, uint32_t logical_block_num,
                               uint32_t physical_block_num, t_log* logger, uint32_t query_id);

/**
 * Elimina un hard link de un bloque lógico.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @param logical_block_num Número de bloque lógico a eliminar
 * @param physical_block_num Número de bloque físico (para logging)
 * @param logger Logger para mensajes
 * @return true si fue exitoso, false en caso contrario
 */
bool remove_logical_block_link(const char* punto_montaje, const char* file_name,
                               const char* tag_name, uint32_t logical_block_num,
                               uint32_t physical_block_num, t_log* logger, uint32_t query_id);

/**
 * Verifica si un File existe.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @return true si existe, false en caso contrario
 */
bool file_exists(const char* punto_montaje, const char* file_name);

/**
 * Verifica si un Tag existe dentro de un File.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 * @return true si existe, false en caso contrario
 */
bool tag_exists(const char* punto_montaje, const char* file_name, const char* tag_name);

/**
 * Inicializa el gestor global de locks FILE:TAG (singleton).
 * Debe llamarse una sola vez al arrancar Storage.
 */
void file_tag_lock_manager_init(void);

/**
 * Destruye el gestor global de locks FILE:TAG.
 * Debe llamarse al apagar Storage.
 */
void file_tag_lock_manager_destroy(void);

/**
 * Adquiere el lock para un FILE:TAG específico (blocking).
 * 
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 */
void lock_file_tag(const char* file_name, const char* tag_name);

/**
 * Libera el lock para un FILE:TAG específico.
 * 
 * @param file_name Nombre del File
 * @param tag_name Nombre del Tag
 */
void unlock_file_tag(const char* file_name, const char* tag_name);

#endif // FILE_METADATA_H
