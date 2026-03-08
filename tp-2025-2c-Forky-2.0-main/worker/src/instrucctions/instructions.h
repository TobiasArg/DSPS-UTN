#ifndef INSTRUCCTION_H
#define INSTRUCCTION_H

#include <commons/log.h> 
#include <utils/sockets.h>
#include <utils/protocol.h>
#include "../main.h"
#include "../../../storage/src/handlers/handlers.h"

typedef enum {
    INST_CREATE,
    INST_TRUNCATE,
    INST_WRITE,
    INST_FLUSH,
    INST_COMMIT,
    INST_READ,
    INST_TAG,
    INST_DELETE,
    INST_END,
    INST_UNK
} t_inst_tipo;

// Retorna: 0=OK (finalizada), 1=Desalojada, -1=Error
int ejecutar_tarea(uint32_t query_id, char *path, uint32_t *pc, int storage_fd, int master_fd,t_log *logger, t_config_worker *config,uint32_t id_worker);
char** leer_script(const char* path_scripts, const char* nombre_instruccion);
int execute_create(const char *instruccion, t_log *logger, int storage_fd);
int execute_truncate(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id);
int execute_write(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id);
int execute_read(const char *instruccion, t_log *logger, int storage_fd, int master_fd, uint32_t query_id);
int execute_read_with_error(const char *instruccion, t_log *logger, int storage_fd, int master_fd, uint32_t query_id, char *error_msg_out, size_t error_msg_size);
int execute_flush(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id);
int execute_commit(const char *instruccion, t_log *logger, int storage_fd, uint32_t query_id);
int execute_tag(const char *instruccion, t_log *logger, int storage_fd);
int execute_delete(const char *instruccion, t_log *logger, int storage_fd);

// Función para obtener el último error de Storage
const char* get_last_storage_error(void);

// Funciones auxiliares
void handle_create_file_tag(const char* origen, const char* destino);
int flush_all_modified_pages(t_log *logger, int storage_fd);

#endif /* INSTRUCCTION_H */