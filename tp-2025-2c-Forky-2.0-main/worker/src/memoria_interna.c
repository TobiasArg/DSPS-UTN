#include "main.h"
#include <utils/include.h>
#include <utils/logger.h>
#include <utils/metadata.h>
#include <commons/collections/list.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// ========================================
// VARIABLES GLOBALES DE MEMORIA
// ========================================
t_memoria_interna *memoria_worker = NULL;
uint32_t current_query_id = 0;  // ID de la query actualmente en ejecución
static char ultimo_error_storage[512] = {0};  // Último error reportado por Storage
static uint64_t contador_accesos = 0;  // Contador para LRU con mayor precisión que time()

// ========================================
// FUNCIONES AUXILIARES PRIVADAS
// ========================================

static void _simular_retardo_memoria(void) {
    if (memoria_worker && memoria_worker->retardo_memoria > 0) {
        usleep(memoria_worker->retardo_memoria * 1000); // ms a microsegundos
    }
}

static t_pagina* _buscar_pagina(const char* file_tag, uint32_t numero_pagina) {
    t_tabla_paginas* tabla = memoria_buscar_tabla_paginas(file_tag);
    if (!tabla) return NULL;
    
    for (int i = 0; i < list_size(tabla->paginas); i++) {
        t_pagina* p = list_get(tabla->paginas, i);
        if (p->numero_pagina == numero_pagina) {
            return p;
        }
    }
    return NULL;
}

// ========================================
// IMPLEMENTACIÓN DE FUNCIONES PÚBLICAS
// ========================================

int memoria_interna_init(uint32_t tamanio_memoria, uint32_t tamanio_pagina, uint32_t retardo_memoria, t_algoritmo_reemplazo algoritmo, int storage_fd, t_log* log) {
    // Crear estructura principal
    memoria_worker = malloc(sizeof(t_memoria_interna));
    if (!memoria_worker) return -1;
    
    // Asignar el bloque único de memoria
    memoria_worker->memoria_base = malloc(tamanio_memoria);
    if (!memoria_worker->memoria_base) {
        free(memoria_worker);
        memoria_worker = NULL;
        return -1;
    }
    
    // Cargo y guardo los parametros del archivo de configuración
    memoria_worker->tamanio_total = tamanio_memoria;
    memoria_worker->tamanio_pagina = tamanio_pagina; // Este es el block_size de Storage
    memoria_worker->total_marcos = tamanio_memoria / tamanio_pagina;
    memoria_worker->retardo_memoria = retardo_memoria;
    memoria_worker->algoritmo_reemplazo = algoritmo;
    memoria_worker->clock_pointer = 0;
    memoria_worker->storage_fd = storage_fd;
    memoria_worker->logger = log;
    
    // Crear arrays de control
    memoria_worker->marcos_ocupados = calloc(memoria_worker->total_marcos, sizeof(bool));
    memoria_worker->marco_a_pagina = calloc(memoria_worker->total_marcos, sizeof(t_pagina*));
    memoria_worker->tablas_paginas = list_create();
    
    if (!memoria_worker->marcos_ocupados || !memoria_worker->marco_a_pagina || !memoria_worker->tablas_paginas) {
        memoria_interna_destroy();
        return -1;
    }
    
    pthread_mutex_init(&memoria_worker->mutex_memoria, NULL);
    memoria_worker->inicializada = true;
    
    return 0;
}

// Limpieza de memoria interna:
void memoria_interna_destroy(void) {
    if (!memoria_worker) return;
    
    if (memoria_worker->tablas_paginas) {
        list_destroy_and_destroy_elements(memoria_worker->tablas_paginas, (void*)memoria_destruir_tabla_paginas);
    }
    
    free(memoria_worker->marcos_ocupados);
    free(memoria_worker->marco_a_pagina);
    free(memoria_worker->memoria_base);
    pthread_mutex_destroy(&memoria_worker->mutex_memoria);
    free(memoria_worker);
    memoria_worker = NULL;
}

// Se crea una nueva tabla de páginas para un File:Tag dado ==> puede crecer dinamicamente
t_tabla_paginas* memoria_crear_tabla_paginas(const char* file_tag, uint32_t total_paginas) {
    if (!memoria_worker || !file_tag) return NULL;
    
    // Verificar si ya existe
    t_tabla_paginas* existente = memoria_buscar_tabla_paginas(file_tag);
    if (existente) return existente;
    
    t_tabla_paginas* tabla = malloc(sizeof(t_tabla_paginas));
    if (!tabla) return NULL;
    
    tabla->file_tag = strdup(file_tag);
    tabla->tamanio_pagina = memoria_worker->tamanio_pagina;
    tabla->total_paginas = total_paginas;
    tabla->paginas = list_create();
    
    if (!tabla->file_tag || !tabla->paginas) {
        free(tabla->file_tag);
        list_destroy(tabla->paginas);
        free(tabla);
        return NULL;
    }
    
    list_add(memoria_worker->tablas_paginas, tabla);
    return tabla;
}

// Buscar una tabla de páginas por File:Tag
t_tabla_paginas* memoria_buscar_tabla_paginas(const char* file_tag) {
    if (!memoria_worker || !file_tag) return NULL;
    
    for (int i = 0; i < list_size(memoria_worker->tablas_paginas); i++) {
        t_tabla_paginas* tabla = list_get(memoria_worker->tablas_paginas, i);
        if (strcmp(tabla->file_tag, file_tag) == 0) {
            return tabla;
        }
    }
    return NULL;
}

// Destruir una tabla de páginas y sus páginas asociadas
void memoria_destruir_tabla_paginas(t_tabla_paginas* tabla) {
    if (!tabla) return;
    
    if (tabla->paginas) {
        for (int i = 0; i < list_size(tabla->paginas); i++) {
            t_pagina* pagina = list_get(tabla->paginas, i);
            free(pagina->file_tag);
            free(pagina);
        }
        list_destroy(tabla->paginas);
    }
    
    free(tabla->file_tag);
    free(tabla);
}


// ========================================
// FUNCIONES DE ACCESO A MEMORIA CON PAGINACIÓN
// ========================================

//  memoria_leer_pagina:(file_tag, numero_pagina, desplazamiento(de 0 a tam_pag -1), buffer, tamanio): Lee datos de una página específica
int memoria_leer_pagina(const char* file_tag, uint32_t numero_pagina, uint32_t offset, void* buffer, uint32_t tamanio) {
    if (!memoria_worker || !file_tag || !buffer) return -1;

    // Validar que la lectura no exceda el límite de la página
    if (offset + tamanio > memoria_worker->tamanio_pagina) {
        log_error(logger, "[MEMORIA] Error: Intento de leer fuera de límites de página %u (offset=%u tamanio=%u tamanio_pagina=%u)", 
                  numero_pagina, offset, tamanio, memoria_worker->tamanio_pagina);
        return -1;
    }

    // Ante lecturas y escrituras hay que simular retardo..
    _simular_retardo_memoria();
    // Evita la condicion de carrera en caso de que varias queries accedan a memoria simultaneamente
    pthread_mutex_lock(&memoria_worker->mutex_memoria);
    
    t_tabla_paginas* tabla = memoria_buscar_tabla_paginas(file_tag);
    if (!tabla) {
        pthread_mutex_unlock(&memoria_worker->mutex_memoria);
        return -1;
    }
    
    t_pagina* pagina = _buscar_pagina(file_tag, numero_pagina);
    if (!pagina) {
        pagina = memoria_crear_pagina(numero_pagina, file_tag);
        if (!pagina) {
            pthread_mutex_unlock(&memoria_worker->mutex_memoria);
            return -1;
        }
        list_add(tabla->paginas, pagina);
    }
    
    if (!pagina->presente) {
        // Log obligatorio de memoria miss ==> PF ==> la pagina no esta en memoria
        char file_copy[256];
        strncpy(file_copy, file_tag, sizeof(file_copy) - 1);
        file_copy[sizeof(file_copy) - 1] = '\0';
        
        char *file = strtok(file_copy, ":");
        char *tag = strtok(NULL, ":");
        
        if (file && tag) {
            log_info(logger, "\x1b[32m## Query <%u>: - Memoria Miss - File: %s - Tag: %s - Pagina: %u\x1b[0m",
                     current_query_id, file, tag, numero_pagina);
        }
        
        if (memoria_cargar_pagina(pagina) < 0) {
            // El error específico ya está guardado en ultimo_error_storage
            pthread_mutex_unlock(&memoria_worker->mutex_memoria);
            return -1;
        }
    }
    
    // Limpiar error anterior si la carga fue exitosa
    ultimo_error_storage[0] = '\0';
    
    pagina->uso = true;
    pagina->ultimo_acceso = ++contador_accesos;  // Contador incremental para LRU preciso
    
    void* direccion = (char*)memoria_worker->memoria_base + (pagina->marco * memoria_worker->tamanio_pagina) + offset;
    memcpy(buffer, direccion, tamanio);
    
    // Log obligatorio de lectura
    char valor_ascii[64] = "";
    
    // Construir versión ASCII
    uint32_t limite_ascii = (tamanio > 32 ? 32 : tamanio);
    for (uint32_t i = 0; i < limite_ascii; i++) {
        unsigned char c = ((unsigned char*)buffer)[i];
        if (c >= 32 && c <= 126) {  // Caracteres imprimibles
            strncat(valor_ascii, (char*)&c, 1);
        } else {
            strcat(valor_ascii, ".");
        }
    }
    if (tamanio > 32) strcat(valor_ascii, "...");
    
    uint32_t dir_fisica = pagina->marco * memoria_worker->tamanio_pagina + offset;
    log_info(logger, "\x1b[32m## Query <%u>: Acción: LEER - Dirección Física: <%u> - Valor: \"%s\"\x1b[0m", 
             current_query_id, dir_fisica, valor_ascii);
    
    // Log de estado de bits después de lectura
    log_info(logger, "\x1b[36m## Query <%u>: Estado Página %u - Bit de Uso: %d - Bit Modificado: %d\x1b[0m",
             current_query_id, numero_pagina, pagina->uso ? 1 : 0, pagina->modificada ? 1 : 0);
    
    pthread_mutex_unlock(&memoria_worker->mutex_memoria);
    return 0;
}

int memoria_escribir_pagina(const char* file_tag, uint32_t numero_pagina, uint32_t offset, const void* datos, uint32_t tamanio) {
    if (!memoria_worker || !file_tag || !datos) return -1;
    
    // Validar que la escritura no exceda el límite de la página
    if (offset + tamanio > memoria_worker->tamanio_pagina) {
        log_error(logger, "[MEMORIA] Error: Intento de escribir fuera de límites de página %u (offset=%u tamanio=%u tamanio_pagina=%u)", 
                  numero_pagina, offset, tamanio, memoria_worker->tamanio_pagina);
        return -1;
    }
    
    _simular_retardo_memoria();
    
    pthread_mutex_lock(&memoria_worker->mutex_memoria);
    
    t_tabla_paginas* tabla = memoria_buscar_tabla_paginas(file_tag);
    if (!tabla) {
        // Crear tabla de páginas automáticamente si no existe
        // Usamos un tamaño por defecto, se puede ajustar dinámicamente
        tabla = memoria_crear_tabla_paginas(file_tag, 1024); // 1024 páginas por defecto
        if (!tabla) {
            pthread_mutex_unlock(&memoria_worker->mutex_memoria);
            return -1;
        }
    }
    
    t_pagina* pagina = _buscar_pagina(file_tag, numero_pagina);
    if (!pagina) {
        pagina = memoria_crear_pagina(numero_pagina, file_tag);
        if (!pagina) {
            pthread_mutex_unlock(&memoria_worker->mutex_memoria);
            return -1;
        }
        list_add(tabla->paginas, pagina);
    }
    
    if (!pagina->presente) {
        // Log obligatorio de memoria miss ==> PF ==> la pagina no esta en memoria
        char file_copy[256];
        // sizeof(file_copy) - 1 previene error de overflow en el buffer
        strncpy(file_copy, file_tag, sizeof(file_copy) - 1);
        file_copy[sizeof(file_copy) - 1] = '\0'; // forzamos el fin de string
        
        char *file = strtok(file_copy, ":");
        char *tag = strtok(NULL, ":");
        
        if (file && tag) {
            log_info(logger, "\x1b[32m## Query <%u>: - Memoria Miss - File: %s - Tag: %s - Pagina: %u\x1b[0m",
                     current_query_id, file, tag, numero_pagina);
        }
        
        if (memoria_cargar_pagina(pagina) < 0) {
            // El error específico ya está guardado en ultimo_error_storage
            pthread_mutex_unlock(&memoria_worker->mutex_memoria);
            return -1;
        }
    }
    
    // Limpiar error anterior si la carga fue exitosa
    ultimo_error_storage[0] = '\0';
    
    pagina->uso = true;
    pagina->modificada = true;
    pagina->ultimo_acceso = ++contador_accesos;  // Contador incremental para LRU preciso
    
    // Calcular dirección física y escribir datos
    // Dirección física = base_memoria + (marco × tam_página) + offset
    void* direccion = (char*)memoria_worker->memoria_base + (pagina->marco * memoria_worker->tamanio_pagina) + offset;
    memcpy(direccion, datos, tamanio);
    
    // Log obligatorio de escritura
    char valor_ascii[64] = "";
    
    // Construir versión ASCII
    uint32_t limite_ascii = (tamanio > 32 ? 32 : tamanio);
    for (uint32_t i = 0; i < limite_ascii; i++) {
        unsigned char c = ((unsigned char*)datos)[i];
        if (c >= 32 && c <= 126) {  // Caracteres imprimibles
            strncat(valor_ascii, (char*)&c, 1);
        } else {
            strcat(valor_ascii, ".");
        }
    }
    if (tamanio > 32) strcat(valor_ascii, "...");
    
    uint32_t dir_fisica = pagina->marco * memoria_worker->tamanio_pagina + offset;
    log_info(logger, "\x1b[32m## Query <%u>: Acción: ESCRIBIR - Dirección Física: <%u> - Valor: \"%s\"\x1b[0m",
             current_query_id, dir_fisica, valor_ascii);
    
    // Log de estado de bits después de escritura
    log_info(logger, "\x1b[36m## Query <%u>: Estado Página %u - Bit de Uso: %d - Bit Modificado: %d\x1b[0m",
             current_query_id, numero_pagina, pagina->uso ? 1 : 0, pagina->modificada ? 1 : 0);
    
    pthread_mutex_unlock(&memoria_worker->mutex_memoria);
    return 0;
}

uint32_t memoria_marcos_libres(void) {
    if (!memoria_worker) return 0;
    
    uint32_t libres = 0;
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        if (!memoria_worker->marcos_ocupados[i]) {
            libres++;
        }
    }
    return libres;
}

t_algoritmo_reemplazo parse_algoritmo_reemplazo(const char* algoritmo_str) {
    if (algoritmo_str && strcmp(algoritmo_str, "CLOCK-M") == 0) {
        return ALGORITMO_CLOCK_M;
    }
    return ALGORITMO_LRU;
}

t_pagina* memoria_crear_pagina(uint32_t numero_pagina, const char* file_tag) {
    t_pagina* pagina = malloc(sizeof(t_pagina));
    if (!pagina) return NULL;
    
    pagina->numero_pagina = numero_pagina;
    pagina->marco = 0;
    pagina->presente = false;
    pagina->modificada = false;
    pagina->uso = false;
    pagina->file_tag = strdup(file_tag);
    pagina->ultimo_acceso = 0;  // Se actualizará cuando sea cargada o accedida
    
    if (!pagina->file_tag) {
        free(pagina);
        return NULL;
    }
    
    return pagina;
}

// Algoritmo First Fit ==> retorna el primer marco libre.
uint32_t memoria_obtener_marco_libre(void) {
    if (!memoria_worker) return UINT32_MAX;
    // recorro la lista de marcos hasta encontrar uno libre
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        if (!memoria_worker->marcos_ocupados[i]) {
            return i;
        }
    }
    return UINT32_MAX;  // No hay marcos libres
}

int memoria_cargar_pagina(t_pagina* pagina) {
    if (!memoria_worker || !pagina || pagina->presente) return -1;
    
    uint32_t marco = memoria_obtener_marco_libre();
    if (marco == UINT32_MAX) {
        return memoria_reemplazar_pagina(pagina);
    }
    
    memoria_worker->marcos_ocupados[marco] = true;
    memoria_worker->marco_a_pagina[marco] = pagina;
    
    pagina->marco = marco;
    pagina->presente = true;
    pagina->ultimo_acceso = ++contador_accesos;  // Contador incremental para LRU preciso
    
    // Extraer File y Tag del file_tag (formato: "FILE:TAG")
    char file_copy[256];
    strncpy(file_copy, pagina->file_tag, sizeof(file_copy) - 1);
    file_copy[sizeof(file_copy) - 1] = '\0';
    
    char *file = strtok(file_copy, ":");
    char *tag = strtok(NULL, ":");
    
    // Logs obligatorios de asignación de marco y memoria add
    if (file && tag) {
        log_info(logger, "\x1b[33m## Query <%u>: Se asigna el Marco: %u a la Página: %u perteneciente al - File: %s - Tag: %s\x1b[0m", 
                 current_query_id, marco, pagina->numero_pagina, file, tag);
        log_info(logger, "\x1b[32m## Query <%u>: - Memoria Add - File: %s - Tag: %s - Pagina: %u - Marco: %u\x1b[0m",
                 current_query_id, file, tag, pagina->numero_pagina, marco);
    }
    
    // Cargar datos desde Storage
    // Calculo la direccion física del marco
    void* direccion = (char*)memoria_worker->memoria_base + (marco * memoria_worker->tamanio_pagina);
    
    if (memoria_worker->storage_fd > 0) {
        // NUEVO PROTOCOLO PAGINACIÓN: [OP][file_len][file][tag_len][tag][block_num]
        // Separar file:tag
        char file_tag_copy[256];
        strncpy(file_tag_copy, pagina->file_tag, sizeof(file_tag_copy) - 1);
        file_tag_copy[sizeof(file_tag_copy) - 1] = '\0';
        
        char *file = strtok(file_tag_copy, ":");
        char *tag = strtok(NULL, ":");
        
        if (!file || !tag) {
            log_error(logger, "[Page Fault] Error parseando file:tag '%s'", pagina->file_tag);
            return -1;
        }
        
        uint8_t op = OP_STORAGE_READ;
        uint32_t query_id_net = htonl(current_query_id);
        uint32_t file_len = strlen(file);
        uint32_t tag_len = strlen(tag);
        uint32_t file_len_net = htonl(file_len);
        uint32_t tag_len_net = htonl(tag_len);
        uint32_t block_num_net = htonl(pagina->numero_pagina);
        
        // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num]
        // Continua solamente si todos los send son exitosos ==> >0
        if (send(memoria_worker->storage_fd, &op, 1, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, &query_id_net, 4, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, &file_len_net, 4, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, file, file_len, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, tag, tag_len, MSG_NOSIGNAL) > 0 &&
            send(memoria_worker->storage_fd, &block_num_net, 4, MSG_NOSIGNAL) > 0) {
            
            uint8_t response; // variable para la respuesta del storage
            if (recv(memoria_worker->storage_fd, &response, 1, MSG_WAITALL) == 1) {
                if (response == ST_OK) {
                    uint32_t data_size_net;
                    if (recv(memoria_worker->storage_fd, &data_size_net, 4, MSG_WAITALL) == 4) {
                        uint32_t data_size = ntohl(data_size_net);
                        if (recv(memoria_worker->storage_fd, direccion, data_size, MSG_WAITALL) == (ssize_t)data_size) {
                            // Éxito: datos cargados desde Storage
                            if (data_size < memoria_worker->tamanio_pagina) {
                                memset((char*)direccion + data_size, 0, memoria_worker->tamanio_pagina - data_size);
                            }
                        } else {
                            // Error al recibir datos
                            log_error(logger, "[Page Fault] Error al recibir datos desde Storage para página %u", pagina->numero_pagina);
                            return -1;
                        }
                    } else {
                        log_error(logger, "[Page Fault] Error al recibir tamaño de datos desde Storage para página %u", pagina->numero_pagina);
                        return -1;
                    }
                } else {
                    // Storage reportó error (ST_ERROR) - la página no existe o está fuera del límite
                    log_error(logger, "\x1b[1;31m[Page Fault] Storage reportó ST_ERROR (0x%02X) para página %u de %s - Página fuera del límite\x1b[0m", 
                             response, pagina->numero_pagina, pagina->file_tag);
                    
                    // Guardar error para propagación
                    snprintf(ultimo_error_storage, sizeof(ultimo_error_storage),
                             "ERROR: Storage reportó bloque %u fuera de límite para %s",
                             pagina->numero_pagina, pagina->file_tag);
                    return -1;
                }
            } else {
                log_error(logger, "[Page Fault] Error al recibir respuesta de Storage para página %u", pagina->numero_pagina);
                return -1;
            }
        } else {
            // Error de comunicación con Storage
            log_error(logger, "[Page Fault] Error de comunicación con Storage para página %u", pagina->numero_pagina);
            return -1;
        }
    } else {
        // No hay conexión a Storage
        log_error(logger, "[Page Fault] No hay conexión a Storage para cargar página %u", pagina->numero_pagina);
        return -1;
    }
    
    return 0;
}

int memoria_descargar_pagina(t_pagina* pagina) {
    if (!memoria_worker || !pagina || !pagina->presente) return -1;
    
    uint32_t marco = pagina->marco;
    
    memoria_worker->marcos_ocupados[marco] = false;
    memoria_worker->marco_a_pagina[marco] = NULL;
    
    pagina->presente = false;
    pagina->marco = 0;
    
    // Extraer File y Tag del file_tag (formato: "FILE:TAG")
    char file_copy[256];
    strncpy(file_copy, pagina->file_tag, sizeof(file_copy) - 1);
    file_copy[sizeof(file_copy) - 1] = '\0';
    
    char *file = strtok(file_copy, ":");
    char *tag = strtok(NULL, ":");
    
    // Log obligatorio de liberación de marco
    if (file && tag) {
        log_info(logger, "\x1b[32m## Query <%u>: Se libera el Marco: %u perteneciente al - File: %s - Tag: %s\x1b[0m", 
                 current_query_id, marco, file, tag);
    }
    
    return 0;
}

// ========================================
// ALGORITMOS DE REEMPLAZO DE PÁGINAS
// ========================================

// ALGORITMO DE REEMPLAZO LRU
// Selecciona la página con el tiempo de último acceso más antiguo
t_pagina* memoria_seleccionar_victima_lru(void) {
    if (!memoria_worker) return NULL;
    
    t_pagina* victima = NULL;
    uint64_t tiempo_mas_antiguo = UINT64_MAX;  // Buscar el mínimo (contador más bajo = más antiguo)
    
    for (int i = 0; i < list_size(memoria_worker->tablas_paginas); i++) {
        t_tabla_paginas* tabla = list_get(memoria_worker->tablas_paginas, i);
        for (int j = 0; j < list_size(tabla->paginas); j++) {
            t_pagina* pagina = list_get(tabla->paginas, j);
            if (pagina->presente && pagina->ultimo_acceso < tiempo_mas_antiguo) {
                tiempo_mas_antiguo = pagina->ultimo_acceso;
                victima = pagina;
            }
        }
    }
    
    return victima;
}

// ALGORITMO DE REEMPLAZO CLOCK-M
// Implementación del algoritmo CLOCK modificado (CLOCK-M)
// Usa 4 pasadas fijas para garantizar terminación determinista:
//   Pasada 1: Buscar (U=0, M=0) sin modificar bits
//   Pasada 2: Buscar (U=0, M=1) limpiando U=1 → U=0 secuencialmente
//   Pasada 3: Buscar (U=0, M=0) - ahora debe existir si hay páginas limpias
//   Pasada 4: Buscar (U=0, M=1) - fallback garantizado si todas tienen M=1
// Esta implementación es más robusta que el ciclo infinito porque:
//   - Tiempo de ejecución acotado y predecible (máximo 4 vueltas)
//   - No puede entrar en loops problemáticos por condiciones de carrera
//   - Más fácil de debuggear y testear
//   - Falla rápido si hay bugs (return NULL después de 4 pasadas)
t_pagina* memoria_seleccionar_victima_clock_m(void) {
    if (!memoria_worker) return NULL;
    
    // Primera pasada: buscar (U=0, M=0) sin tocar bits
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        t_pagina* pagina_actual = memoria_worker->marco_a_pagina[memoria_worker->clock_pointer];
        
        if (pagina_actual && pagina_actual->presente) {
            if (!pagina_actual->uso && !pagina_actual->modificada) {
                memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
                return pagina_actual;
            }
        }
        
        memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
    }
    
    // Segunda pasada: buscar (U=0, M=1) Y limpiar U=1 → U=0 secuencialmente
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        t_pagina* pagina_actual = memoria_worker->marco_a_pagina[memoria_worker->clock_pointer];
        
        if (pagina_actual && pagina_actual->presente) {
            if (!pagina_actual->uso && pagina_actual->modificada) {
                memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
                return pagina_actual;
            }
            if (pagina_actual->uso) {
                pagina_actual->uso = false;
            }
        }
        
        memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
    }
    
    // Tercera pasada: buscar (U=0, M=0) - ahora debe existir si hay páginas con M=0
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        t_pagina* pagina_actual = memoria_worker->marco_a_pagina[memoria_worker->clock_pointer];
        
        if (pagina_actual && pagina_actual->presente) {
            if (!pagina_actual->uso && !pagina_actual->modificada) {
                memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
                return pagina_actual;
            }
        }
        
        memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
    }
    
    // Cuarta pasada: buscar (U=0, M=1) - debe existir porque todas tienen U=0 ahora
    for (uint32_t i = 0; i < memoria_worker->total_marcos; i++) {
        t_pagina* pagina_actual = memoria_worker->marco_a_pagina[memoria_worker->clock_pointer];
        
        if (pagina_actual && pagina_actual->presente) {
            if (!pagina_actual->uso && pagina_actual->modificada) {
                memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
                return pagina_actual;
            }
        }
        
        memoria_worker->clock_pointer = (memoria_worker->clock_pointer + 1) % memoria_worker->total_marcos;
    }
    
    return NULL;
}

int memoria_reemplazar_pagina(t_pagina* nueva_pagina) {
    if (!memoria_worker || !nueva_pagina) return -1;
    
    t_pagina* victima = NULL;
    if (memoria_worker->algoritmo_reemplazo == ALGORITMO_LRU) {
        victima = memoria_seleccionar_victima_lru();
    } else if (memoria_worker->algoritmo_reemplazo == ALGORITMO_CLOCK_M) {
        victima = memoria_seleccionar_victima_clock_m();
    }
    
    if (!victima) return -1;
    
    uint32_t marco_victima = victima->marco;
    
    // Si la página víctima está modificada, escribirla a Storage
    if (victima->modificada && memoria_worker->storage_fd > 0) {
        _simular_retardo_memoria();
        
        // NUEVO PROTOCOLO PAGINACIÓN: [OP][file_len][file][tag_len][tag][block_num][block_data]
        // Separar file:tag
        char file_tag_copy[256];
        strncpy(file_tag_copy, victima->file_tag, sizeof(file_tag_copy) - 1);
        file_tag_copy[sizeof(file_tag_copy) - 1] = '\0';
        
        char *file = strtok(file_tag_copy, ":");
        char *tag = strtok(NULL, ":");
        
        if (!file || !tag) {
            log_error(logger, "[SWAP] Error parseando file:tag '%s'", victima->file_tag);
        } else {
            // Obtener datos de la página víctima
            void* datos_victima = (char*)memoria_worker->memoria_base + (marco_victima * memoria_worker->tamanio_pagina);
            
            uint8_t op = OP_STORAGE_WRITE;
            uint32_t query_id_net = htonl(current_query_id);
            uint32_t file_len = strlen(file);
            uint32_t tag_len = strlen(tag);
            uint32_t file_len_net = htonl(file_len);
            uint32_t tag_len_net = htonl(tag_len);
            uint32_t block_num_net = htonl(victima->numero_pagina);
            
            // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num][block_data]
            // Enviar solicitud de escritura a Storage
            if (send(memoria_worker->storage_fd, &op, 1, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &query_id_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &file_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, file, file_len, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, tag, tag_len, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &block_num_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, datos_victima, memoria_worker->tamanio_pagina, MSG_NOSIGNAL) > 0) {
            
            // Esperar confirmación del Storage
            uint8_t response;
            if (recv(memoria_worker->storage_fd, &response, 1, MSG_WAITALL) == 1) {
                if (response == ST_OK) {
                    log_debug(memoria_worker->logger, "Query %u: Página víctima %s/%u escrita exitosamente a Storage", 
                              current_query_id, victima->file_tag, victima->numero_pagina);
                } else {
                    log_warning(memoria_worker->logger, "Query %u: Storage reportó error al escribir página %s/%u", 
                                current_query_id, victima->file_tag, victima->numero_pagina);
                }
                }
            } else {
                log_warning(memoria_worker->logger, "Query %u: Error al enviar página modificada %s/%u a Storage", 
                            current_query_id, victima->file_tag, victima->numero_pagina);
            }
        }
    }
    
    victima->presente = false;
    victima->modificada = false;
    victima->marco = 0;
    
    // Log obligatorio de reemplazo
    log_info(logger, "\x1b[32m## Query <%u>: Se reemplaza la página %s/%u por la %s/%u\x1b[0m", 
             current_query_id, 
             victima->file_tag, victima->numero_pagina,
             nueva_pagina->file_tag, nueva_pagina->numero_pagina);
    
    memoria_worker->marco_a_pagina[marco_victima] = nueva_pagina;
    nueva_pagina->marco = marco_victima;
    nueva_pagina->presente = true;
    nueva_pagina->ultimo_acceso = ++contador_accesos;  // Contador incremental para LRU preciso
    
    // Log obligatorio de asignación de marco después del reemplazo
    char file_copy_reemplazo[256];
    strncpy(file_copy_reemplazo, nueva_pagina->file_tag, sizeof(file_copy_reemplazo) - 1);
    file_copy_reemplazo[sizeof(file_copy_reemplazo) - 1] = '\0';
    
    char *file_reemplazo = strtok(file_copy_reemplazo, ":");
    char *tag_reemplazo = strtok(NULL, ":");
    
    if (file_reemplazo && tag_reemplazo) {
        log_info(logger, "\x1b[33m## Query <%u>: Se asigna el Marco: %u a la Página: %u perteneciente al - File: %s - Tag: %s\x1b[0m", 
                 current_query_id, marco_victima, nueva_pagina->numero_pagina, file_reemplazo, tag_reemplazo);
        log_info(logger, "\x1b[32m## Query <%u>: - Memoria Add - File: %s - Tag: %s - Pagina: %u - Marco: %u\x1b[0m",
                 current_query_id, file_reemplazo, tag_reemplazo, nueva_pagina->numero_pagina, marco_victima);
    }
    
    // Cargar nueva página desde Storage
    void* direccion = (char*)memoria_worker->memoria_base + (marco_victima * memoria_worker->tamanio_pagina);
    
    if (memoria_worker->storage_fd > 0) {
        // NUEVO PROTOCOLO PAGINACIÓN: [OP][file_len][file][tag_len][tag][block_num]
        // Separar file:tag
        char file_tag_copy2[256];
        strncpy(file_tag_copy2, nueva_pagina->file_tag, sizeof(file_tag_copy2) - 1);
        file_tag_copy2[sizeof(file_tag_copy2) - 1] = '\0';
        
        char *file = strtok(file_tag_copy2, ":");
        char *tag = strtok(NULL, ":");
        
        if (!file || !tag) {
            log_error(logger, "[SWAP] Error parseando file:tag '%s' para nueva página", nueva_pagina->file_tag);
            memset(direccion, 0, memoria_worker->tamanio_pagina);
        } else {
            uint8_t op = OP_STORAGE_READ;
            uint32_t query_id_net = htonl(current_query_id);
            uint32_t file_len = strlen(file);
            uint32_t tag_len = strlen(tag);
            uint32_t file_len_net = htonl(file_len);
            uint32_t tag_len_net = htonl(tag_len);
            uint32_t block_num_net = htonl(nueva_pagina->numero_pagina);
            
            // NUEVO PROTOCOLO: [OP][query_id][file_len][file][tag_len][tag][block_num]
            if (send(memoria_worker->storage_fd, &op, 1, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &query_id_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &file_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, file, file_len, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &tag_len_net, 4, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, tag, tag_len, MSG_NOSIGNAL) > 0 &&
                send(memoria_worker->storage_fd, &block_num_net, 4, MSG_NOSIGNAL) > 0) {
            
            uint8_t response;
            if (recv(memoria_worker->storage_fd, &response, 1, MSG_WAITALL) == 1 && response == ST_OK) {
                uint32_t data_size_net;
                if (recv(memoria_worker->storage_fd, &data_size_net, 4, MSG_WAITALL) == 4) {
                    uint32_t data_size = ntohl(data_size_net);
                    if (recv(memoria_worker->storage_fd, direccion, data_size, MSG_WAITALL) == (ssize_t)data_size) {
                        // Éxito: nueva página cargada
                        if (data_size < memoria_worker->tamanio_pagina) {
                            memset((char*)direccion + data_size, 0, memoria_worker->tamanio_pagina - data_size);
                        }
                    } else {
                        memset(direccion, 0, memoria_worker->tamanio_pagina);
                    }
                } else {
                    memset(direccion, 0, memoria_worker->tamanio_pagina);
                }
                } else {
                    memset(direccion, 0, memoria_worker->tamanio_pagina);
                }
            } else {
                memset(direccion, 0, memoria_worker->tamanio_pagina);
            }
        }
    } else {
        memset(direccion, 0, memoria_worker->tamanio_pagina);
    }
    
    return 0;
}

//  Paginas que fueron modificadas y hay que escribirlas en el storage
t_list* memoria_get_paginas_modificadas(const char* file_tag) {
    t_list* paginas_modificadas = list_create();
    t_tabla_paginas* tabla = memoria_buscar_tabla_paginas(file_tag);
    if (!tabla) return paginas_modificadas;

    for (int i = 0; i < list_size(tabla->paginas); i++) {
        t_pagina* pagina = list_get(tabla->paginas, i);
        if (pagina->modificada) {
            list_add(paginas_modificadas, pagina);
        }
    }

    return paginas_modificadas;
}

void* memoria_get_datos_pagina(int marco) {
    if (!memoria_worker || marco >= memoria_worker->total_marcos)
        return NULL;

    return (char*)memoria_worker->memoria_base + (marco * memoria_worker->tamanio_pagina);
}

const char* memoria_get_ultimo_error_storage(void) {
    return ultimo_error_storage[0] != '\0' ? ultimo_error_storage : NULL;
}
