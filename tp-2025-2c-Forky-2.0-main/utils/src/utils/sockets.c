#include "./sockets.h"
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>

int create_connection(t_log *logger, char *port, char *ip)
{
    if (!ip || strlen(ip) == 0)
    {
        // log_error(logger, " (SCKT): Fallo al crear conexión: IP es nula o vacía.");
        return -1;
    }

    if (!port || strlen(port) == 0)
    {
        // log_error(logger, " (SCKT): Fallo al crear conexión: Puerto es nulo o vacío.");
        return -1;
    }

    struct addrinfo hints, *serverinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int resultado = getaddrinfo(ip, port, &hints, &serverinfo);
    if (resultado != 0)
    {
        // log_error(logger, " (SCKT): Fallo en getaddrinfo para %shost=%s%s %sprt=%s%s: %s",
        //           COLOR_LIGHT_GRAY, COLOR_RESET, ip,
        //           COLOR_LIGHT_GRAY, COLOR_RESET, port,
        //           gai_strerror(resultado));
        return -1;
    }

    int socket_cliente = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
    if (socket_cliente == -1)
    {
        // log_error(logger, " (SCKT): Fallo al crear socket para %shost=%s%s %sprt=%s%s",
        //           COLOR_LIGHT_GRAY, COLOR_RESET, ip,
        //           COLOR_LIGHT_GRAY, COLOR_RESET, port);
        freeaddrinfo(serverinfo);
        return -1;
    }

    if (connect(socket_cliente, serverinfo->ai_addr, serverinfo->ai_addrlen) == -1)
    {
        // log_error(logger, "[SOCKET] No se pudo conectar al servidor. %shost=%s%s %sprt=%s%s",
        //           COLOR_LIGHT_GRAY, COLOR_RESET, ip,
        //           COLOR_LIGHT_GRAY, COLOR_RESET, port);
        close(socket_cliente);
        freeaddrinfo(serverinfo);
        return -1;
    }

    freeaddrinfo(serverinfo);
    // log_info(logger, "[SOCKET] Módulo %sConectado exitosamente!%s. %shost=%s%s %sprt=%s%s %ssocket=%s%d%s",
    //          COLOR_DEBUG, COLOR_RESET,
    //          COLOR_LIGHT_GRAY, COLOR_RESET, ip,
    //          COLOR_LIGHT_GRAY, COLOR_RESET, port,
    //          COLOR_LIGHT_GRAY, COLOR_RESET, socket_cliente, COLOR_RESET);

    return socket_cliente;
}

void destroy_connection(t_log *logger, int *socket_fd)
{
    if (!socket_fd || *socket_fd < 0)
    {
        log_debug(logger, "[SOCKET] Intentando destruir conexión nula o inválida.");
        return;
    }

    // log_warning(logger, "[SOCKET] La conexión fue cerrada exitosamente!. %ssocket=%s%d%s",
    //             COLOR_LIGHT_GRAY, COLOR_RESET, *socket_fd, COLOR_RESET);
    close(*socket_fd);
    *socket_fd = -1;
}

int wait_custommer(t_log *logger, int socket_servidor)
{
    struct sockaddr_in dir_cliente;
    socklen_t tam_direccion = sizeof(dir_cliente);

    int socket_cliente = accept(socket_servidor, (struct sockaddr *)&dir_cliente, &tam_direccion);
    if (socket_cliente == -1)
    {
        log_error(logger, "[SOCKET] Fallo al aceptar nueva conexión. %serror=%s%s",
                  COLOR_LIGHT_GRAY, COLOR_RESET, strerror(errno));
    }

    return socket_cliente;
}

int listen_server(t_log *logger, int connection, char *module)
{
    int client = wait_custommer(logger, connection);
    if (client != -1)
    {
        // log_info(logger, "[SOCKET] Módulo '%s'. %sConectado exitosamente!%s %scliente=%s%d%s.",
        //          module, COLOR_DEBUG, COLOR_RESET,COLOR_LIGHT_GRAY, COLOR_RESET, client, COLOR_RESET);
    }
    else
    {
        // log_warning(logger, "[SOCKET] No se pudo establecer conexión con el módulo '%s'.", module);
    }
    return client;
}

int start_server(t_log *logger, char *ip, char *puerto)
{
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int resultado = getaddrinfo(ip, puerto, &hints, &servinfo);
    if (resultado != 0)
    {
        log_error(logger, "[SOCKET] Fallo en getaddrinfo para %shost=%s%s %sprt=%s%s: %s",
                  COLOR_LIGHT_GRAY, COLOR_RESET, ip,
                  COLOR_LIGHT_GRAY, COLOR_RESET, puerto,
                  gai_strerror(resultado));
        return -1;
    }

    int socket_servidor = -1;
    bool conectado = false;

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        socket_servidor = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (socket_servidor == -1)
        {
            log_debug(logger, "[SOCKET] Error al crear socket. Intentando siguiente.");
            continue;
        }

        int opt = 1;
        if (setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            log_debug(logger, "[SOCKET] Warning setsockopt falló (non-fatal).");
            /* continue;  no es crítico en general */
        }

        if (bind(socket_servidor, p->ai_addr, p->ai_addrlen) == -1)
        {
            log_debug(logger, "[SOCKET] Error al enlazar socket. Intentando siguiente.");
            close(socket_servidor);
            socket_servidor = -1;
            continue;
        }

        conectado = true;
        break;
    }

    if (!conectado)
    {
        // log_error(logger, "[SOCKET] No se pudo enlazar el socket a ninguna dirección para %shost=%s%s %sprt=%s%s",
        //           COLOR_LIGHT_GRAY, COLOR_RESET, ip,
        //           COLOR_LIGHT_GRAY, COLOR_RESET, puerto);
        freeaddrinfo(servinfo);
        return -1;
    }

    if (listen(socket_servidor, SOMAXCONN) == -1)
    {
        log_error(logger, "[SOCKET] Fallo al escuchar en %ssocket=%s%d%s para %shost=%s%s %sprt=%s%s",
                  COLOR_LIGHT_GRAY, COLOR_RESET, socket_servidor, COLOR_RESET,
                  COLOR_LIGHT_GRAY, COLOR_RESET, ip,
                  COLOR_LIGHT_GRAY, COLOR_RESET, puerto);
        close(socket_servidor);
        freeaddrinfo(servinfo);
        return -1;
    }

    // log_info(logger, "[SOCKET] Escuchando conexiones. %shost=%s%s %sprt=%s%s %ssocket=%s%d%s",
    //          COLOR_LIGHT_GRAY, COLOR_RESET, ip,
    //          COLOR_LIGHT_GRAY, COLOR_RESET, puerto,
    //          COLOR_LIGHT_GRAY, COLOR_RESET, socket_servidor, COLOR_RESET);

    freeaddrinfo(servinfo);
    return socket_servidor;
}

bool is_connection_active(t_log *logger, int socket_fd)
{
    if (socket_fd < 0) return false;

    char buf[1];
    // Usar MSG_PEEK para NO consumir datos del buffer
    int result = recv(socket_fd, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);

    if (result == 0) {
        // Cliente cerró conexión limpiamente (EOF)
        log_debug(logger, "[SOCKET] Socket %d: Conexión cerrada por el cliente (EOF)", socket_fd);
        return false;
    } else if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No hay datos disponibles, pero la conexión está activa
            return true;
        } else {
            // Error real de socket (ECONNRESET, EPIPE, etc.)
            log_warning(logger, "[SOCKET] Socket %d error: %s", socket_fd, strerror(errno));
            return false;
        }
    }

    // Si hay datos disponibles (result > 0), la conexión está activa
    // Los datos NO se consumen gracias a MSG_PEEK
    return true;
}

