#ifndef FS_INIT_H
#define FS_INIT_H

#include <stdint.h>
#include <stdbool.h>
#include <commons/log.h>

/**
 * Inicializa el File System según el valor de FRESH_START.
 * Si FRESH_START=TRUE, formatea el FS desde cero.
 * Si FRESH_START=FALSE, lee las estructuras existentes.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param fresh_start Si es true, formatea el FS
 * @param logger Logger para mensajes
 * @return true si la inicialización fue exitosa, false en caso contrario
 */
bool fs_init(const char* punto_montaje, bool fresh_start, t_log* logger);

/**
 * Formatea el File System desde cero.
 * Crea todos los directorios y archivos necesarios.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param logger Logger para mensajes
 * @return true si el formateo fue exitoso, false en caso contrario
 */
bool fs_format(const char* punto_montaje, t_log* logger);

/**
 * Carga las estructuras existentes del File System.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param logger Logger para mensajes
 * @return true si la carga fue exitosa, false en caso contrario
 */
bool fs_load_existing(const char* punto_montaje, t_log* logger);

#endif // FS_INIT_H
