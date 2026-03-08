#include "./connections.h"
#include <string.h>

void process_result(t_log* logger, int socket_fd, uint8_t op_code, char** out_final_reason);

int connect_master_server(t_log *logger, t_config_query_control *config)
{
    if (!logger)
    {
        fprintf(stderr, "[x] Logger no inicializado.\n");
        return -1;
    }

    if (!config)
    {
        log_error(logger, "[x] No se recibió una configuración válida para conectar al Master.");
        return -1;
    }

    char *puerto_str = string_from_format("%d", config->puerto_master);
    int server = create_connection(logger, puerto_str, config->ip_master);
    free(puerto_str);

    if (server < 0)
    {
        log_error(logger, "[x] Fallo al conectar con el Master. <%s:%d>", config->ip_master, config->puerto_master);
        return -1;
    }

    log_info(logger, "## Conexión al Master exitosa. IP: %s, Puerto: %d", config->ip_master, config->puerto_master);
    return server;
}


void listen_to_master(t_log* logger, int socket_fd, char** out_final_reason) {
    *out_final_reason = NULL; 

    while (true) {
        uint8_t op_code; 
        int bytes_received = recv(socket_fd, &op_code, sizeof(uint8_t), 0);

        if (bytes_received <= 0) {
            if (*out_final_reason == NULL) {
                *out_final_reason = strdup("Se perdió la conexión con el Master.");
            }
            break;
        }
        
        process_result(logger, socket_fd, op_code, out_final_reason);
        
        // Si se recibió el resultado final, terminar el loop
        if (*out_final_reason != NULL) {
            break;
        }
    }
}
void process_result(t_log* logger, int socket_fd, uint8_t op_code, char** out_final_reason) {
    uint32_t query_id;
    char* contenido = NULL;

    switch (op_code) {
        case OP_QUERY_READ_MESSAGE:
            // Mensaje intermedio de lectura (durante ejecución de la query)
            if (protocol_recv_query_read_message(socket_fd, &query_id, &contenido) == 0) {
                // El contenido incluye file:tag y datos leídos
                // Buscar el separador entre file:tag y contenido
                char* separador = strchr(contenido, '|');
                char file_tag[256] = "DESCONOCIDO";
                char* datos = contenido;
                
                if (separador) {
                    // Extraer file:tag
                    size_t tag_len = separador - contenido;
                    if (tag_len < sizeof(file_tag)) {
                        strncpy(file_tag, contenido, tag_len);
                        file_tag[tag_len] = '\0';
                        datos = separador + 1; // Datos después del separador
                    }
                } else {
                    // Si no hay separador, asumir que todo es el file:tag o error
                    strncpy(file_tag, contenido, sizeof(file_tag) - 1);
                    file_tag[sizeof(file_tag) - 1] = '\0';
                    datos = "";
                }
                
                log_info(logger, "## Lectura realizada: File %s, contenido: %s", 
                         file_tag, datos);
                
                free(contenido);
            } else {
                log_error(logger, "Error al procesar OP_QUERY_READ_MESSAGE.");
            }
            break;

        case OP_QUERY_RESULT:
            // Mensaje final de resultado (SUCCESS, ERROR, etc.)
            if (protocol_recv_query_result(socket_fd, &query_id, &contenido) > 0) {
                // Este es el mensaje de finalización de la query
                // log_info(logger, "[✓] Resultado final recibido: %s", contenido);
                
                // Guardar como motivo de finalización
                *out_final_reason = strdup(contenido);
                
                free(contenido);
            } else {
                log_error(logger, "Error al procesar OP_QUERY_RESULT.");
            }
            break;

        default:
            log_warning(logger, "Se recibió una operación desconocida del Master: %d", op_code);
            break;
    }
}

void disconnect_master_server(t_log *logger, int master_conn)
{
    if (master_conn < 0) {
        log_warning(logger, "[x] Conexión inválida o ya cerrada.");
        return;
    }
    destroy_connection(logger, &master_conn);
}
/*
bool is_master_server_active(t_log *logger, int master_conn)
{
    return is_connection_active(logger, master_conn);
}*/