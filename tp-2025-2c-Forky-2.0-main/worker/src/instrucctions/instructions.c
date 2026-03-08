#include "./instructions.h"
#include <sys/select.h>
#include <unistd.h>
#include <ctype.h>

// Declaración estática de función auxiliar
static t_inst_tipo identificar_instruccion(const char *linea);
static int chequear_desalojo_master(int fd, uint32_t query_id, uint32_t pc, t_log *logger);

int ejecutar_tarea(uint32_t query_id, char *path, uint32_t *pc,
                   int storage_fd, int master_fd,
                   t_log *logger, t_config_worker *config,
                   uint32_t id_worker)
{
    // Lectura del script
    char **instrucciones = leer_script(config->path_scripts, path);
    if (!instrucciones)
    {
        log_error(logger, "[x] No se pudo abrir el script, Path=%s", path);
        return -1;
    }

    // Setear query_id global para logs de memoria
    current_query_id = query_id;

    int resultado = 0; // 0=OK, 1=Desalojada, -1=Error

    // Ejecución secuencial: comenzar desde el PC guardado
    for (uint32_t i = *pc; instrucciones[i]; ++i)
    {
        *pc = i;

        // Antes de ejecutar, revisar si Master solicita desalojo
        if (chequear_desalojo_master(master_fd, query_id, *pc, logger) > 0)
        {
            flush_all_modified_pages(logger, storage_fd);
            log_info(logger, "\x1b[33m## Query <%u>: Desalojada por pedido del Master\x1b[0m", query_id);
            resultado = 1; // Desalojada
            break;
        }

        t_inst_tipo tipo = identificar_instruccion(instrucciones[i]);

        const char *nombre =
            tipo == INST_CREATE   ? "CREATE"   :
            tipo == INST_TRUNCATE ? "TRUNCATE" :
            tipo == INST_WRITE    ? "WRITE"    :
            tipo == INST_FLUSH    ? "FLUSH"    :
            tipo == INST_COMMIT   ? "COMMIT"   :
            tipo == INST_READ     ? "READ"     :
            tipo == INST_TAG      ? "TAG"      :
            tipo == INST_DELETE   ? "DELETE"   :
            tipo == INST_END      ? "END"      : "UNKNOWN";

        log_info(logger, "\x1b[36m## Query <%u>: FETCH - Program Counter: <%u> - <%s>\x1b[0m",
                 query_id, i, instrucciones[i]);

        switch (tipo)
        {
        case INST_CREATE:
            if (execute_create(instrucciones[i], logger, storage_fd) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: CREATE falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: CREATE falló en Storage (archivo ya existe) - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_TRUNCATE:
            if (execute_truncate(instrucciones[i], logger, storage_fd, query_id) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: TRUNCATE falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: TRUNCATE falló en Storage - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_WRITE:
            if (execute_write(instrucciones[i], logger, storage_fd, query_id) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: WRITE falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: WRITE falló en Storage (archivo commiteado) - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_READ:
            if (execute_read(instrucciones[i], logger, storage_fd, master_fd, query_id) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: READ falló - Terminando query\x1b[0m", query_id);
                
                // Obtener mensaje de error de Storage si está disponible
                const char* storage_err = get_last_storage_error();
                char error_msg[512];
                
                if (storage_err && strlen(storage_err) > 0) {
                    // Usar el mensaje de error específico de Storage
                    snprintf(error_msg, sizeof(error_msg), "%s - Instrucción: %s", storage_err, instrucciones[i]);
                } else {
                    // Fallback al mensaje genérico
                    snprintf(error_msg, sizeof(error_msg), "ERROR: READ falló en Storage - Instrucción: %s", instrucciones[i]);
                }
                
                // Notificar al Master que la query finalizó con ERROR usando OP_WORKER_QUERY_ERROR
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_FLUSH:
            if (execute_flush(instrucciones[i], logger, storage_fd, query_id) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: FLUSH falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: FLUSH falló en Storage - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_COMMIT:
            if (execute_commit(instrucciones[i], logger, storage_fd, query_id) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: COMMIT falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: COMMIT falló en Storage - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_TAG:
            if (execute_tag(instrucciones[i], logger, storage_fd) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: TAG falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: TAG falló en Storage (tag ya existe) - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_DELETE:
            if (execute_delete(instrucciones[i], logger, storage_fd) < 0) {
                log_error(logger, "\x1b[1;31m[ERROR] Query %u: DELETE falló - Terminando query\x1b[0m", query_id);
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "ERROR: DELETE falló en Storage - Instrucción: %s", 
                        instrucciones[i]);
                if (protocol_send_worker_query_error(master_fd, query_id, error_msg) < 0) {
                    log_error(logger, "[WORKER] Error al notificar error de query %u al Master", query_id);
                } else {
                    log_warning(logger, "\x1b[1;33m⚠ Query %u terminó con errores - ERROR enviado al Master\x1b[0m", query_id);
                }
                resultado = -1;  // Marcar error
                goto query_finished;
            }
            break;
        case INST_END:
            log_info(logger, "\x1b[36m## Query <%u>: - Instrucción realizada: <%s>\x1b[0m",
                     query_id, instrucciones[i]);

            log_info(logger, "\x1b[1;32m## Query %u: ==== Finalizada exitosamente ==== \x1b[0m", query_id);

            log_info(logger, "[WORKER] Enviando OP_WORKER_QUERY_FINISHED (0x%02X) al Master por socket %d, query_id=%u",
                     OP_WORKER_QUERY_FINISHED, master_fd, query_id);

            if (protocol_send_worker_query_finished(master_fd, query_id) < 0) {
                log_error(logger, "[WORKER] Error al notificar fin de query %u al Master", query_id);
            } else {
                log_info(logger, "\x1b[1;32m✓ Query %u finalizada - Notificación enviada al Master\x1b[0m", query_id);
                log_info(logger, "\x1b[1;33mWORKER DISPONIBLE PARA ATENDER MÁS QUERIES\x1b[0m");
            }

            goto query_finished;
        default:
            log_warning(logger, "[x] Instrucción desconocida PC=%u, Texto=%s",
                        i, instrucciones[i]);
            break;
        }

        if (tipo != INST_END && tipo != INST_UNK) {
            log_info(logger, "\x1b[36m## Query <%u>: - Instrucción realizada: <%s>\x1b[0m",
                     query_id, nombre);
        }
    }

query_finished:
    free(instrucciones[0]);
    free(instrucciones);
    return resultado;
}


// Revisión periódica de desalojo
static int chequear_desalojo_master(int fd, uint32_t query_id, uint32_t pc, t_log *logger)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int ready = select(fd + 1, &set, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0; // No hay actividad de desalojo
    }

    uint32_t recv_qid = 0;
    uint32_t recv_version = 0;

    int res = protocol_recv_master_evict(fd, &recv_qid, &recv_version);
    if (res == 0 && recv_qid == query_id)
    {
        if (protocol_send_worker_evict_ack(fd, query_id, pc, recv_version) == 0)
        {
            log_debug(logger, "[EVICT] [WORKER] ACK enviado correctamente — query_id=%u pc=%u", query_id, pc);
        }
        else
        {
            log_error(logger, "[EVICT] [WORKER] Error al enviar ACK de desalojo — query_id=%u", query_id);
        }

        return 1; // Desalojo confirmado
    }

    return 0; // Nada que hacer
}


// Identificación de instrucción
static t_inst_tipo identificar_instruccion(const char *linea)
{
    if (strncmp(linea, "CREATE ", 7) == 0)
        return INST_CREATE;
    if (strncmp(linea, "TRUNCATE ", 9) == 0)
        return INST_TRUNCATE;
    if (strncmp(linea, "WRITE ", 6) == 0)
        return INST_WRITE;
    if (strncmp(linea, "FLUSH ", 6) == 0)
        return INST_FLUSH;
    if (strncmp(linea, "COMMIT ", 7) == 0)
        return INST_COMMIT;
    if (strncmp(linea, "READ ", 5) == 0)
        return INST_READ;
    if (strncmp(linea, "TAG ", 4) == 0)
        return INST_TAG;
    if (strncmp(linea, "DELETE ", 7) == 0)
        return INST_DELETE;
    if (strncmp(linea, "END", 3) == 0)
        return INST_END;
    return INST_UNK;
}

static char *read_file_to_string(const char *full_path, size_t *len_out)
{
    FILE *f = fopen(full_path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc(len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, len, f);
    buf[read] = '\0';
    if (len_out)
        *len_out = read;
    fclose(f);
    return buf;
}

static char **split_lines(char *text, const char *delim)
{
    int lines = 0;
    char *tmp = strtok(strdup(text), delim);
    while (tmp)
    {
        ++lines;
        tmp = strtok(NULL, delim);
    }
    char **array = malloc((lines + 1) * sizeof(char *));
    if (!array)
        return NULL;
    int i = 0;
    tmp = strtok(text, delim);
    while (tmp)
    {
        array[i++] = tmp;
        tmp = strtok(NULL, delim);
    }
    array[i] = NULL;
    return array;
}

char **leer_script(const char *path_scripts, const char *nombre_instruccion)
{
    size_t len = strlen(path_scripts) + strlen(nombre_instruccion) + 2;
    char *full_path = malloc(len);
    snprintf(full_path, len, "%s/%s", path_scripts, nombre_instruccion);
    size_t file_len;
    char *contenido = read_file_to_string(full_path, &file_len);
    free(full_path);
    if (!contenido)
        return NULL;
    char **lineas = split_lines(contenido, "\r\n");
    if (!lineas)
    {
        free(contenido);
        return NULL;
    }
    return lineas;
}

void handle_create_file_tag(const char *origen, const char *destino)
{ 
    // Punto de extensión para lógica local antes de comunicarse con Storage
    (void)origen;
    (void)destino;
}