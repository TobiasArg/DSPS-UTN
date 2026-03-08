#ifndef WORKER_H
#define WORKER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> 
#include <stdbool.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <pthread.h>
#include <time.h>
#include <utils/protocol.h>

extern t_log* logger;

// ========================================
// MEMORIA INTERNA DEL WORKER - PAGINACIÓN
// ========================================
typedef enum {
    ALGORITMO_LRU,
    ALGORITMO_CLOCK_M
} t_algoritmo_reemplazo;

// Estructura para una página en memoria física
typedef struct {
    uint32_t numero_pagina;      // Número lógico de página
    uint32_t marco;              // Marco físico donde está cargada
    bool presente;               // Si la página está en memoria física
    bool modificada;             // Bit de modificación (dirty bit)
    bool uso;                    // Bit de uso (para algoritmos de reemplazo)
    char* file_tag;              // File:Tag al que pertenece esta página
    time_t ultimo_acceso;        // Timestamp del último acceso (para LRU)
} t_pagina;

// Tabla de páginas para un File:Tag específico
typedef struct {
    char* file_tag;              // Identificador "archivo:tag"
    t_list* paginas;             // Lista de t_pagina
    uint32_t tamanio_pagina;     // Tamaño de página (obtenido del Storage durante handshake)
    uint32_t total_paginas;      // Total de páginas de este File:Tag
} t_tabla_paginas;

// Estructura principal de la memoria interna
typedef struct {
    void* memoria_base;          // Puntero al inicio del malloc único
    uint32_t tamanio_total;      // Tamaño total de la memoria (tam_memoria config)
    uint32_t tamanio_pagina;     // Tamaño de página (del Storage)
    uint32_t total_marcos;       // Total de marcos físicos disponibles
    uint32_t retardo_memoria;    // Retardo en microsegundos por acceso
    
    // Control de marcos físicos
    bool* marcos_ocupados;       // Array de marcos ocupados
    t_pagina** marco_a_pagina;   // Mapeo de marco físico a página
    
    // Algoritmo de reemplazo
    t_algoritmo_reemplazo algoritmo_reemplazo;
    uint32_t clock_pointer;      // Puntero para algoritmo CLOCK-M
    
    // Tablas de páginas por File:Tag
    t_list* tablas_paginas;      // Lista de t_tabla_paginas
    
    pthread_mutex_t mutex_memoria; // Mutex para acceso concurrente
    bool inicializada;             // Flag para verificar si está inicializada

    int storage_fd;
    t_log* logger;
} t_memoria_interna;

// Variable global de memoria interna
extern t_memoria_interna* memoria_worker;
extern uint32_t current_query_id;

// ========================================
// ESTRUCTURA PRINCIPAL WORKER
// ========================================
typedef struct t_config_worker {
    char* ip_master;
    int puerto_master;
    char* ip_storage;
    int puerto_storage;
    int tam_memoria;
    int retardo_memoria;
    char* algoritmo_reemplazo;
    char* path_scripts;
    char* log_level;
} t_config_worker;

// ========================================
// FUNCIONES DE CONFIGURACIÓN
// ========================================
t_config_worker* cargar_configuracion(char* path_config);
uint32_t parse_worker_id(const char* arg);

// ========================================
// FUNCIONES DE MEMORIA INTERNA
// ========================================

// Inicialización y destrucción
int memoria_interna_init(uint32_t tamanio_memoria, uint32_t tamanio_pagina, uint32_t retardo_memoria, t_algoritmo_reemplazo algoritmo, int storage_fd, t_log* log);
void memoria_interna_destroy(void);

// Gestión de tablas de páginas
t_tabla_paginas* memoria_crear_tabla_paginas(const char* file_tag, uint32_t total_paginas);
t_tabla_paginas* memoria_buscar_tabla_paginas(const char* file_tag);
void memoria_destruir_tabla_paginas(t_tabla_paginas* tabla);

// Gestión de páginas individuales
t_pagina* memoria_crear_pagina(uint32_t numero_pagina, const char* file_tag);
int memoria_cargar_pagina(t_pagina* pagina);
int memoria_descargar_pagina(t_pagina* pagina);
uint32_t memoria_obtener_marco_libre(void);

// Algoritmos de reemplazo
t_pagina* memoria_seleccionar_victima_lru(void);
t_pagina* memoria_seleccionar_victima_clock_m(void);
int memoria_reemplazar_pagina(t_pagina* nueva_pagina);
t_algoritmo_reemplazo parse_algoritmo_reemplazo(const char* algoritmo_str);

// Acceso a memoria con paginación
int memoria_leer_pagina(const char* file_tag, uint32_t numero_pagina, uint32_t offset, void* buffer, uint32_t tamanio);
int memoria_escribir_pagina(const char* file_tag, uint32_t numero_pagina, uint32_t offset, const void* datos, uint32_t tamanio);

// Obtener último error de Storage
const char* memoria_get_ultimo_error_storage(void);

// Utilidades y debugging
void memoria_interna_dump(void);
uint32_t memoria_marcos_libres(void);
void memoria_dump_tabla_paginas(const char* file_tag);


t_list* memoria_get_paginas_modificadas(const char* file_tag);
void* memoria_get_datos_pagina(int marco);

#endif /* WORKER_H */
