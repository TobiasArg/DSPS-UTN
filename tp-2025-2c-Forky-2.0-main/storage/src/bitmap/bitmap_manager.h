#ifndef BITMAP_MANAGER_H
#define BITMAP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <commons/bitarray.h>
#include <commons/log.h>
#include <pthread.h>

/**
 * Estructura que maneja el bitmap del File System.
 * Usa t_bitarray de commons para gestionar el estado de los bloques físicos.
 */
typedef struct {
    t_bitarray* bitarray;
    char* bitmap_path;
    uint32_t num_blocks;
    t_log* logger;
    pthread_mutex_t mtx;   // Protege acceso al bitarray
} t_bitmap_manager;

/**
 * Inicializa el bitmap manager cargando el bitmap.bin desde disco.
 * 
 * @param punto_montaje Ruta raíz del FS
 * @param num_blocks Cantidad total de bloques físicos
 * @param logger Logger para mensajes
 * @return Puntero al bitmap manager o NULL en caso de error
 */
t_bitmap_manager* bitmap_manager_init(const char* punto_montaje, uint32_t num_blocks, t_log* logger);

/**
 * Destruye el bitmap manager y libera recursos.
 * 
 * @param manager Bitmap manager a destruir
 */
void bitmap_manager_destroy(t_bitmap_manager* manager);

/**
 * Busca un bloque libre, lo reserva y devuelve su número
 * 
 * @param manager Bitmap manager
 * @return Número del bloque reservado, o -1 si no hay bloques libres
 */
int reservar_bloque_libre(t_bitmap_manager* manager, uint32_t query_id);

/**
 * Encuentra el primer bloque libre en el bitmap (función auxiliar).
 * 
 * @param manager Bitmap manager
 * @return Número del primer bloque libre, o -1 si no hay bloques libres
 */
int32_t bitmap_find_free_block(t_bitmap_manager* manager);

/**
 * Marca un bloque como ocupado en el bitmap.
 * 
 * @param manager Bitmap manager
 * @param block_num Número de bloque a marcar como ocupado
 * @return true si fue exitoso, false en caso contrario
 */
bool bitmap_set_block_used(t_bitmap_manager* manager, uint32_t block_num);

/**
 * @param manager Bitmap manager
 * @param numero Número de bloque a liberar
 * @return true si fue exitoso, false en caso contrario
 */
bool liberar_bloque(t_bitmap_manager* manager, uint32_t numero, uint32_t query_id);

/**
 * Marca un bloque como libre en el bitmap (función auxiliar).
 * 
 * @param manager Bitmap manager
 * @param block_num Número de bloque a marcar como libre
 * @return true si fue exitoso, false en caso contrario
 */
bool bitmap_set_block_free(t_bitmap_manager* manager, uint32_t block_num);

/**
 * Verifica si un bloque está ocupado.
 * 
 * @param manager Bitmap manager
 * @param block_num Número de bloque a verificar
 * @return true si está ocupado, false si está libre
 */
bool bitmap_is_block_used(t_bitmap_manager* manager, uint32_t block_num);

/**
 * Sincroniza el bitmap en memoria con el archivo bitmap.bin en disco.
 * 
 * @param manager Bitmap manager
 * @return true si fue exitoso, false en caso contrario
 */
bool bitmap_sync_to_disk(t_bitmap_manager* manager);

/**
 * Cuenta la cantidad de bloques libres en el bitmap.
 * 
 * @param manager Bitmap manager
 * @return Cantidad de bloques libres
 */
uint32_t bitmap_count_free_blocks(t_bitmap_manager* manager);

// Reconstruye el bitmap recorriendo physical_blocks (st_nlink > 1 => usado)
bool bitmap_rebuild_from_physical_blocks(t_bitmap_manager* manager, const char* physical_blocks_dir);

#endif // BITMAP_MANAGER_H
