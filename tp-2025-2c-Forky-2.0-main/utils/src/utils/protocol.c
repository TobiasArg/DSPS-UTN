#include "protocol.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h> 

#ifndef MSG_NOSIGNAL
#endif

// Worker -> Storage
int protocol_send_worker_handshake(int fd, uint32_t worker_id) {
    uint8_t op = OP_WORKER_HANDSHAKE;
    uint32_t id_net = htonl(worker_id);
    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &id_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    return 0;
}

int protocol_recv_worker_handshake(int fd, uint32_t* out_worker_id) {
    uint8_t op = ST_ERROR;
    uint32_t id_net = 0;
    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_WORKER_HANDSHAKE) return -1;
    if (recv(fd, &id_net, 4, 0) <= 0) return -1;
    if (out_worker_id) *out_worker_id = ntohl(id_net);
    return 0;
}

// Storage -> Worker
int protocol_recv_storage_handshake_ok(int fd, uint32_t* out_block_size) {
    uint8_t status = 0;
    uint32_t bs_net = 0;

    if (recv(fd, &status, 1, MSG_WAITALL) <= 0)
        return -1;

    if (status == ST_ERROR)
        return -2; // Error explícito del Storage

    if (status != ST_OK)
        return -1; // Respuesta inválida

    if (recv(fd, &bs_net, 4, MSG_WAITALL) <= 0)
        return -1;

    if (out_block_size)
        *out_block_size = ntohl(bs_net);

    return 0;
}

int protocol_send_storage_handshake_ok(int fd, uint32_t block_size) {
    uint8_t op = ST_OK;
    uint32_t bs_net = htonl(block_size);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &bs_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_send_storage_handshake_error(int fd) {
    uint8_t status = ST_ERROR;
    if (send(fd, &status, 1, MSG_NOSIGNAL) <= 0) return -1;
    return 0;
}

// Worker -> Master: envia su ID
int protocol_send_worker_register_master(int fd, uint32_t worker_id) {
    uint8_t  op     = OP_MASTER_WORKER_REGISTER;
    uint32_t id_net = htonl(worker_id);
    if (send(fd, &op, 1, MSG_NOSIGNAL)     <= 0) return -1;
    if (send(fd, &id_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    return 0;
}

// Master: recibe el ID (worker_id)
int protocol_recv_worker_register(int fd, uint32_t* out_worker_id) {
    uint8_t  op = 0;
    uint32_t id_net = 0;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_MASTER_WORKER_REGISTER) return -1;

    if (recv(fd, &id_net, 4, 0) <= 0) return -1;
    if (out_worker_id) *out_worker_id = ntohl(id_net);
    return 0;
}

// QueryControl -> Master
int protocol_send_query_submit(int fd, const char* query_path, uint32_t priority) {
    uint8_t op = OP_QUERY_SUBMIT;
    uint32_t path_len = htonl(strlen(query_path));
    uint32_t priority_net = htonl(priority);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &path_len, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, query_path, strlen(query_path), MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &priority_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Master -> QueryControl
int protocol_recv_query_submit(int fd, char** out_query_path, uint32_t* out_priority) {
    uint8_t op = 0;
    uint32_t path_len_net = 0;
    uint32_t priority_net = 0;

    
    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_QUERY_SUBMIT) return -1;

    if (recv(fd, &path_len_net, 4, 0) <= 0) return -1;
    uint32_t path_len = ntohl(path_len_net);

    // Esto lo deberia hacer storage? asignar memoria
    char* path = malloc(path_len + 1);
    if (!path) return -1;
    if (recv(fd, path, path_len, 0) <= 0) {
        free(path);
        return -1;
    }
    path[path_len] = '\0'; 

    
    if (recv(fd, &priority_net, 4, 0) <= 0) {
        free(path);
        return -1;
    }

    if (out_query_path) *out_query_path = path;
    if (out_priority) *out_priority = ntohl(priority_net);

    return 0;
}

int protocol_recv_worker_register_ack(int fd, uint32_t *out_worker_id)
{
    if (!out_worker_id) return -1;

    uint8_t op;
    if (recv_all(fd, &op, sizeof(uint8_t)) != sizeof(uint8_t))
        return -1;

    if (op != OP_MASTER_WORKER_REGISTER_ACK)
        return -1;

    uint32_t worker_id_net;
    if (recv_all(fd, &worker_id_net, sizeof(uint32_t)) != sizeof(uint32_t))
        return -1;

    *out_worker_id = ntohl(worker_id_net);
    return 0;
}

// Master -> QueryControl
int protocol_recv_query_confirm(int fd, uint32_t* out_query_id) {
    uint8_t op = 0;
    uint32_t query_id_net = 0;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_QUERY_CONFIRM) return -1;
    if (recv(fd, &query_id_net, 4, 0) <= 0) return -1;

    if (out_query_id) *out_query_id = ntohl(query_id_net);
    return 0;
}

int protocol_send_query_confirm(int fd, uint32_t query_id) {
    uint8_t op = OP_QUERY_CONFIRM;
    uint32_t query_id_net = htonl(query_id);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Master -> Worker: enviar ACK de registro
int protocol_send_worker_register_ack(int fd, uint32_t worker_id) {
    uint8_t op = OP_MASTER_WORKER_REGISTER_ACK;
    uint32_t id_net = htonl(worker_id);
    
    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &id_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    
    return 0;
}

int protocol_send_master_path(int fd, uint32_t query_id, const char* path, uint32_t pc) {
    uint8_t op = OP_MASTER_SEND_PATH;
    uint32_t len = htonl(strlen(path) + 1); // incluir terminador
    uint32_t pc_net = htonl(pc);
    uint32_t qid_net = htonl(query_id);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &qid_net, 4, MSG_NOSIGNAL) <= 0) return -1; 
    if (send(fd, &len, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, path, strlen(path) + 1, MSG_NOSIGNAL) <= 0) return -1; // +1
    if (send(fd, &pc_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_recv_master_path(int fd,uint32_t* out_query_id, char** out_path, uint32_t* out_pc) {
    if (!out_query_id || !out_path || !out_pc) return -1;

    uint8_t  op = 0;
    uint32_t qid_net = 0;
    uint32_t len_net = 0;
    uint32_t pc_net  = 0;

    // opcode
    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_MASTER_SEND_PATH) return -1;

     // query_id
    if (recv(fd, &qid_net, 4, MSG_WAITALL) <= 0) return -1;
    *out_query_id = ntohl(qid_net);

    // longitud del path
    if (recv(fd, &len_net, 4, 0) <= 0) return -1;
    uint32_t len = ntohl(len_net);

    // path
    char* path = malloc(len);
    if (!path) return -1;
    if (recv_all(fd, path, len) <= 0) { free(path); return -1; }
    // No hace falta agregar \0 porque ya está incluido en len

    // pc
    if (recv(fd, &pc_net, 4, 0) <= 0) { free(path); return -1; }
    uint32_t pc = ntohl(pc_net);

    *out_path = path;
    *out_pc   = pc;
    return 0;
}


int protocol_send_query_result(int fd, uint32_t query_id, const char* result) {
    uint8_t op = OP_QUERY_RESULT;                   
    uint32_t id_net = htonl(query_id);              
    uint32_t result_len = strlen(result);           
    uint32_t result_len_net = htonl(result_len);    

    if (send(fd, &op, sizeof(uint8_t), MSG_NOSIGNAL) <= 0) return -1;       
    if (send(fd, &id_net, sizeof(uint32_t), MSG_NOSIGNAL) <= 0) return -1;   
    if (send(fd, &result_len_net, sizeof(uint32_t), MSG_NOSIGNAL) <= 0) return -1; 
    if (send(fd, result, result_len, MSG_NOSIGNAL) <= 0) return -1;         

    return 0; 
}

int protocol_recv_query_result(int fd, uint32_t* out_query_id, char** out_result) {
    uint32_t id_net;
    if (recv(fd, &id_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t)) {
        return -1; 
    }
    *out_query_id = ntohl(id_net);

    uint32_t result_size_net;
    if (recv(fd, &result_size_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t)) {
        return -1; 
    }
    uint32_t result_size = ntohl(result_size_net); 

    *out_result = malloc(result_size + 1); 
    if (recv(fd, *out_result, result_size, MSG_WAITALL) != result_size) {
        free(*out_result); 
        return -1;
    }
    (*out_result)[result_size] = '\0';
    
    return 1;
}

// Master -> QueryControl: Enviar mensaje de lectura intermedio
int protocol_send_query_read_message(int fd, uint32_t query_id, const char* read_data) {
    uint8_t op = OP_QUERY_READ_MESSAGE;                   
    uint32_t id_net = htonl(query_id);              
    uint32_t data_len = strlen(read_data);           
    uint32_t data_len_net = htonl(data_len);    

    // Usar MSG_DONTWAIT para mensajes READ intermedios - evitar bloqueo con queries largas
    if (send(fd, &op, sizeof(uint8_t), MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Buffer lleno - no es error crítico para mensajes READ intermedios
            usleep(1000); // 1ms delay y reintentar
            if (send(fd, &op, sizeof(uint8_t), MSG_NOSIGNAL) <= 0) return -1;
        } else {
            return -1;
        }
    }
    if (send(fd, &id_net, sizeof(uint32_t), MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) return -1;   
    if (send(fd, &data_len_net, sizeof(uint32_t), MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) return -1; 
    if (send(fd, read_data, data_len, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) return -1;         

    return 0; 
}

// QueryControl <- Master: Recibir mensaje de lectura intermedio
int protocol_recv_query_read_message(int fd, uint32_t* out_query_id, char** out_read_data) {
    // NOTA: El opcode ya fue leído por el caller (listen_to_master)
    // NO leer el opcode aquí para evitar desincronización
    
    uint32_t id_net;
    if (recv(fd, &id_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t)) {
        return -1; 
    }
    *out_query_id = ntohl(id_net);

    uint32_t data_size_net;
    if (recv(fd, &data_size_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t)) {
        return -1; 
    }
    uint32_t data_size = ntohl(data_size_net); 

    *out_read_data = malloc(data_size + 1); 
    if (!*out_read_data) {
        return -1;
    }
    
    if (recv(fd, *out_read_data, data_size, MSG_WAITALL) != data_size) {
        free(*out_read_data); 
        return -1;
    }
    (*out_read_data)[data_size] = '\0';
    
    return 0;
}


int protocol_send_storage_delete(int fd, const char* file_tag, uint32_t query_id) {
    uint8_t op = OP_STORAGE_DELETE;
    uint32_t query_net = htonl(query_id);
    uint32_t len = strlen(file_tag);
    uint32_t len_net = htonl(len);

    if (send(fd, &op,        1,           MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_net, 4,           MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &len_net,   4,           MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, file_tag,   len,         MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_send_storage_tag(int fd, const char* origen, const char* destino) {
    uint8_t op = OP_STORAGE_TAG;
    
    uint32_t origen_len = strlen(origen);
    uint32_t origen_len_net = htonl(origen_len);
    
    uint32_t destino_len = strlen(destino);
    uint32_t destino_len_net = htonl(destino_len);

    if (send(fd, &op, sizeof(uint8_t), 0) <= 0) return -1;
    if (send(fd, &origen_len_net, sizeof(uint32_t), 0) <= 0) return -1;
    if (send(fd, origen, origen_len, 0) <= 0) return -1;
    if (send(fd, &destino_len_net, sizeof(uint32_t), 0) <= 0) return -1;
    if (send(fd, destino, destino_len, 0) <= 0) return -1;

    return 0;
}

// espera una respuesta OK o ERROR de Storage
int protocol_recv_storage_response(int fd) {
    uint8_t op_code;
    if (recv(fd, &op_code, sizeof(uint8_t), MSG_WAITALL) != sizeof(uint8_t)) {
        return -1; 
    }
    return op_code; 
}

int protocol_send_worker_create(int fd, const char *file, const char *tag, uint32_t size, uint32_t query_id)
{
    uint8_t  op   = OP_STORAGE_CREATE;
    uint32_t query_id_net = htonl(query_id); 
    uint32_t f_len = htonl(strlen(file));
    uint32_t t_len = htonl(strlen(tag));
    uint32_t size_net = htonl(size);

    if (send(fd, &op,       1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &f_len,    4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, file, strlen(file), MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &t_len,    4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, tag,  strlen(tag),  MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &size_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_recv_worker_create(int fd, char **out_file, char **out_tag, uint32_t *out_size, uint32_t *out_query_id)
{
    uint32_t query_id_net, f_len_net, t_len_net, size_net;

    //Leer query_id
    if (recv_all(fd, &query_id_net, 4) <= 0) return -1;
    if (out_query_id) *out_query_id = ntohl(query_id_net);

    /* file */
    if (recv_all(fd, &f_len_net, 4) <= 0) return -1;
    uint32_t f_len = ntohl(f_len_net);
    char *file = malloc(f_len + 1);
    if (!file) return -1;
    if (recv_all(fd, file, f_len) < 0) { free(file); return -1; }
    file[f_len] = '\0';
    *out_file = file;

    /* tag */
    if (recv_all(fd, &t_len_net, 4) <= 0) { free(file); return -1; }
    uint32_t t_len = ntohl(t_len_net);
    char *tag = malloc(t_len + 1);
    if (!tag) { free(file); return -1; }
    if (recv_all(fd, tag, t_len) < 0) { free(file); free(tag); return -1; }
    tag[t_len] = '\0';
    *out_tag = tag;

    /* size */
    if (recv_all(fd, &size_net, 4) <= 0) { free(file); free(tag); return -1; }
    *out_size = ntohl(size_net);

    return 0;
}



int protocol_send_worker_commit(int fd, const char *file, const char *tag, uint32_t query_id)
{
    uint8_t  op          = OP_STORAGE_COMMIT;
    uint32_t query_net   = htonl(query_id);
    uint32_t f_len_net   = htonl(strlen(file));
    uint32_t t_len_net   = htonl(strlen(tag));

    if (send(fd, &op,        1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &f_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, file, strlen(file), MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &t_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, tag,  strlen(tag),  MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_recv_worker_commit(int fd, char **out_file, char **out_tag)
{
    uint8_t  op;
    uint32_t f_len_net, t_len_net;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_STORAGE_COMMIT) return -1;

    /* file */
    if (recv(fd, &f_len_net, 4, 0) <= 0) return -1;
    uint32_t f_len = ntohl(f_len_net);
    char *file = malloc(f_len + 1);
    if (!file) return -1;
    if (recv(fd, file, f_len, 0) <= 0) { free(file); return -1; }
    file[f_len] = '\0';
    *out_file = file;

    /* tag */
    if (recv(fd, &t_len_net, 4, 0) <= 0) { free(file); return -1; }
    uint32_t t_len = ntohl(t_len_net);
    char *tag = malloc(t_len + 1);
    if (!tag) { free(file); return -1; }
    if (recv(fd, tag, t_len, 0) <= 0) { free(file); free(tag); return -1; }
    tag[t_len] = '\0';
    *out_tag = tag;

    return 0;
}

int protocol_send_worker_truncate(int fd, const char *file, const char *tag, uint32_t new_size, uint32_t query_id)
{
    uint8_t  op        = OP_STORAGE_TRUNCATE;
    uint32_t query_net = htonl(query_id);
    uint32_t f_len_net = htonl(strlen(file));
    uint32_t t_len_net = htonl(strlen(tag));
    uint32_t size_net  = htonl(new_size);

    if (send(fd, &op,        1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &f_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, file, strlen(file), MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &t_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, tag,  strlen(tag),  MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &size_net,  4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_recv_worker_truncate(int fd, char **out_file, char **out_tag, uint32_t *out_new_size)
{
    uint8_t  op;
    uint32_t f_len_net, t_len_net, size_net;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_STORAGE_TRUNCATE) return -1;

    /* file */
    if (recv(fd, &f_len_net, 4, 0) <= 0) return -1;
    uint32_t f_len = ntohl(f_len_net);
    char *file = malloc(f_len + 1);
    if (!file) return -1;
    if (recv(fd, file, f_len, 0) <= 0) { free(file); return -1; }
    file[f_len] = '\0';
    *out_file = file;

    /* tag */
    if (recv(fd, &t_len_net, 4, 0) <= 0) { free(file); return -1; }
    uint32_t t_len = ntohl(t_len_net);
    char *tag = malloc(t_len + 1);
    if (!tag) { free(file); return -1; }
    if (recv(fd, tag, t_len, 0) <= 0) { free(file); free(tag); return -1; }
    tag[t_len] = '\0';
    *out_tag = tag;

    /* new size */
    if (recv(fd, &size_net, 4, 0) <= 0) { free(file); free(tag); return -1; }
    *out_new_size = ntohl(size_net);

    return 0;
}

/* ------------------------------------------------------------------ */
/* ENVÍO  TAG  (worker -> storage)                                    */
/* ------------------------------------------------------------------ */
int protocol_send_worker_tag(int fd, const char *file_orig, const char *tag_orig,
                             const char *file_dst,  const char *tag_dst, uint32_t query_id)
{
    uint8_t  op        = OP_STORAGE_TAG;
    uint32_t query_net = htonl(query_id);

    uint32_t fo_len_net = htonl(strlen(file_orig));
    uint32_t to_len_net = htonl(strlen(tag_orig));
    uint32_t fd_len_net = htonl(strlen(file_dst));
    uint32_t td_len_net = htonl(strlen(tag_dst));

#define CHK(x)  do { if ((x) <= 0) return -1; } while(0)

    CHK(send(fd, &op,        1, MSG_NOSIGNAL));
    CHK(send(fd, &query_net, 4, MSG_NOSIGNAL));

    CHK(send(fd, &fo_len_net, 4, MSG_NOSIGNAL));
    CHK(send(fd, file_orig,   strlen(file_orig), MSG_NOSIGNAL));

    CHK(send(fd, &to_len_net, 4, MSG_NOSIGNAL));
    CHK(send(fd, tag_orig,    strlen(tag_orig),  MSG_NOSIGNAL));

    CHK(send(fd, &fd_len_net, 4, MSG_NOSIGNAL));
    CHK(send(fd, file_dst,    strlen(file_dst),  MSG_NOSIGNAL));

    CHK(send(fd, &td_len_net, 4, MSG_NOSIGNAL));
    CHK(send(fd, tag_dst,     strlen(tag_dst),   MSG_NOSIGNAL));

#undef CHK
    return 0;
}

/* ------------------------------------------------------------------ */
/* RECEPCIÓN TAG  (storage -> worker)                                 */
/* ------------------------------------------------------------------ */
int protocol_recv_worker_tag(int fd, char **out_file_orig, char **out_tag_orig, char **out_file_dst,  char **out_tag_dst)
{
    uint32_t fo_len_net, to_len_net, fd_len_net, td_len_net;

    /* file origen */
    if (recv_all(fd, &fo_len_net, 4) <= 0) return -1;
    uint32_t fo_len = ntohl(fo_len_net);
    char *file_orig = malloc(fo_len + 1);
    if (!file_orig) return -1;
    if (recv_all(fd, file_orig, fo_len) < 0){ free(file_orig); return -1; }
    file_orig[fo_len] = '\0';
    *out_file_orig = file_orig;

    /* tag origen */
    if (recv_all(fd, &to_len_net, 4) <= 0){ free(file_orig); return -1; }
    uint32_t to_len = ntohl(to_len_net);
    char *tag_orig = malloc(to_len + 1);
    if (!tag_orig){ free(file_orig); return -1; }
    if (recv_all(fd, tag_orig, to_len) < 0){ free(file_orig); free(tag_orig); return -1; }
    tag_orig[to_len] = '\0';
    *out_tag_orig = tag_orig;

    /* file destino */
    if (recv_all(fd, &fd_len_net, 4) <= 0){ free(file_orig); free(tag_orig); return -1; }
    uint32_t fd_len = ntohl(fd_len_net);
    char *file_dst = malloc(fd_len + 1);
    if (!file_dst){ free(file_orig); free(tag_orig); return -1; }
    if (recv_all(fd, file_dst, fd_len) < 0){ free(file_orig); free(tag_orig); free(file_dst); return -1; }
    file_dst[fd_len] = '\0';
    *out_file_dst = file_dst;

    /* tag destino */
    if (recv_all(fd, &td_len_net, 4) <= 0){ free(file_orig); free(tag_orig); free(file_dst); return -1; }
    uint32_t td_len = ntohl(td_len_net);
    char *tag_dst = malloc(td_len + 1);
    if (!tag_dst){ free(file_orig); free(tag_orig); free(file_dst); return -1; }
    if (recv_all(fd, tag_dst, td_len) < 0){ free(file_orig); free(tag_orig); free(file_dst); free(tag_dst); return -1; }
    tag_dst[td_len] = '\0';
    *out_tag_dst = tag_dst;

    return 0;
}

size_t recv_all(int fd, void *buf, size_t len)
{
    size_t  total = 0;
    uint8_t *p    = buf;

    while (total < len) {
        size_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return n;          /* error o EOF */
        total += n;
    }
    return total;
}

// Versión de recv_all con timeout configurable
size_t recv_all_timeout(int fd, void *buf, size_t len, int timeout_ms)
{
    // Configurar timeout en el socket
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    
    // Recibir con timeout
    size_t result = recv_all(fd, buf, len);
    
    // Restaurar modo blocking (timeout 0)
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    return result;
}

size_t send_all(int fd, const void *buf, size_t len)
{
    size_t       total = 0;
    const uint8_t *p   = buf;

    while (total < len) {
        size_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
        if (n < 0) return n;
        total += n;
    }
    return total;
}

int protocol_send_query_assign(int fd, uint32_t query_id, char* query_path) {
    if (!query_path) return -1;

    uint8_t  op          = OP_MASTER_QUERY_ASSIGN;
    uint32_t id_net      = htonl(query_id);
    uint32_t path_len    = (uint32_t)strlen(query_path);      // sin '\0' (igual que send_query_submit)
    uint32_t path_len_net = htonl(path_len);

    if (send(fd, &op,           1,            MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &id_net,       sizeof id_net,MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &path_len_net, sizeof path_len_net, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd,  query_path,   path_len,     MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Worker -> Master: notifica que completó la query y envía resultado
int protocol_recv_query_complete(int fd, uint32_t* out_query_id, char** out_result) {
    if (!out_query_id || !out_result) return -1;

    uint8_t  op = 0;
    uint32_t id_net = 0;
    uint32_t res_len_net = 0;

    // opcode
    if (recv(fd, &op, 1, MSG_WAITALL) != 1) return -1;
    if (op != OP_MASTER_QUERY_COMPLETE) return -1;

    // query_id
    if (recv(fd, &id_net, sizeof id_net, MSG_WAITALL) != sizeof id_net) return -1;
    *out_query_id = ntohl(id_net);

    // result length (bytes, sin '\0' en el wire)
    if (recv(fd, &res_len_net, sizeof res_len_net, MSG_WAITALL) != sizeof res_len_net) return -1;
    uint32_t res_len = ntohl(res_len_net);

    // guarda básica anti-Malloc gigante
    if (res_len > (uint32_t)(64 * 1024 * 1024))
    return -1;

    // buffer + '\0' para que quede C-string
    char* buf = (char*)malloc(res_len + 1);
    if (!buf) return -1;

    if (res_len > 0) {
        if (recv(fd, buf, res_len, MSG_WAITALL) != (ssize_t)res_len) {
            free(buf);
            return -1;
        }
    }
    buf[res_len] = '\0';

    *out_result = buf;
    return 0;
}


////////////////////////* Protocolo de Desalojo */////////////////////////////


// Master -> Worker: solicita desalojo de query_id
int protocol_send_master_evict(int fd, uint32_t query_id, uint32_t version) {
    uint8_t op = OP_MASTER_EVICT;
    uint32_t qid_net = htonl(query_id);
    uint32_t ver_net = htonl(version);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &qid_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &ver_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Worker recibe solicitud de desalojo
int protocol_recv_master_evict(int fd, uint32_t *out_query_id, uint32_t *out_version) {
    if (!out_query_id) return -1;

    uint8_t op = 0;
    uint32_t qid_net = 0;
    uint32_t ver_net = 0;


    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_MASTER_EVICT) return -1;

    if (recv(fd, &qid_net, 4, 0) <= 0) return -1;
    *out_query_id = ntohl(qid_net);

    if (recv(fd, &ver_net, 4, 0) <= 0) return -1;
    *out_version = ntohl(ver_net);

    return 0;
}

// Worker -> Master: confirma desalojo con PC actual
int protocol_send_worker_evict_ack(int fd, uint32_t query_id, uint32_t pc, uint32_t version) {
    uint8_t op = OP_WORKER_EVICT_ACK;
    uint32_t qid_net = htonl(query_id);
    uint32_t pc_net = htonl(pc);
    uint32_t ver_net = htonl(version);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &qid_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &pc_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &ver_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Master recibe confirmación de desalojo
int protocol_recv_worker_evict_ack(int fd, uint32_t *out_query_id, uint32_t *out_pc, uint32_t *out_version) {
    if (!out_query_id || !out_pc) return -1;

    uint8_t op = 0;
    uint32_t qid_net = 0;
    uint32_t pc_net = 0;
    uint32_t ver_net = 0;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_WORKER_EVICT_ACK) return -1;

    if (recv(fd, &qid_net, 4, 0) <= 0) return -1;
    *out_query_id = ntohl(qid_net);

    if (recv(fd, &pc_net, 4, 0) <= 0) return -1;
    *out_pc = ntohl(pc_net);

    if (recv(fd, &ver_net, 4, 0) <= 0) return -1;
    if (out_version) *out_version = ntohl(ver_net);

    return 0;
}

// Master recibe confirmación de desalojo con timeout
int protocol_recv_worker_evict_ack_timeout(int fd, uint32_t *out_query_id, uint32_t *out_pc, int timeout_ms, uint32_t *out_version) {
    if (!out_query_id || !out_pc) return -1;

    // Configurar timeout en el socket
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    
    // Intentar recibir con timeout
    int result = protocol_recv_worker_evict_ack(fd, out_query_id, out_pc, out_version);
    
    // Restaurar modo blocking (timeout 0)
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    return result;
}

// Worker -> Master: envía resultado de READ
int protocol_send_worker_read_result(int fd, uint32_t query_id, const char *file_tag, const void *data, uint32_t size) {
    uint8_t op = OP_WORKER_READ_RESULT;
    uint32_t qid_net = htonl(query_id);
    uint32_t tag_len = strlen(file_tag);
    uint32_t tag_len_net = htonl(tag_len);
    uint32_t size_net = htonl(size);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &qid_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &tag_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, file_tag, tag_len, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &size_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, data, size, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

// Master recibe resultado de READ del Worker
int protocol_recv_worker_read_result(int fd, uint32_t *out_query_id, char **out_file_tag, char **out_data, uint32_t *out_size) {
    if (!out_query_id || !out_file_tag || !out_data || !out_size) return -1;

    uint8_t op = 0;
    uint32_t qid_net = 0, tag_len_net = 0, size_net = 0;

    if (recv(fd, &op, 1, 0) <= 0) return -1;
    if (op != OP_WORKER_READ_RESULT) return -1;

    if (recv(fd, &qid_net, 4, 0) <= 0) return -1;
    *out_query_id = ntohl(qid_net);

    if (recv(fd, &tag_len_net, 4, 0) <= 0) return -1;
    uint32_t tag_len = ntohl(tag_len_net);

    char *file_tag = malloc(tag_len + 1);
    if (!file_tag) return -1;

    if (recv_all(fd, file_tag, tag_len) < 0) {
        free(file_tag);
        return -1;
    }
    file_tag[tag_len] = '\0';
    *out_file_tag = file_tag;

    if (recv(fd, &size_net, 4, 0) <= 0) {
        free(file_tag);
        return -1;
    }
    uint32_t data_size = ntohl(size_net);
    *out_size = data_size;

    char *data = malloc(data_size + 1);
    if (!data) {
        free(file_tag);
        return -1;
    }

    if (recv_all(fd, data, data_size) < 0) {
        free(file_tag);
        free(data);
        return -1;
    }
    data[data_size] = '\0';
    *out_data = data;

    return 0;
}

// ========================================
// Worker → Master: Finalización de Query
// ========================================
int protocol_send_worker_query_finished(int fd, uint32_t query_id) {
    uint8_t op = OP_WORKER_QUERY_FINISHED;
    uint32_t query_id_net = htonl(query_id);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) return -1;

    return 0;
}

int protocol_recv_worker_query_finished(int fd, uint32_t *out_query_id) {
    // Leer opcode
    uint8_t opcode = 0;
    if (recv(fd, &opcode, sizeof(uint8_t), MSG_WAITALL) != sizeof(uint8_t))
        return -1;
    
    if (opcode != OP_WORKER_QUERY_FINISHED)
        return -2;
    
    // Leer query_id
    uint32_t query_id_net;
    if (recv(fd, &query_id_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t))
        return -3;
    
    *out_query_id = ntohl(query_id_net);
    return 0;
}

// ========================================
// Worker → Master: Error en Query
// ========================================
int protocol_send_worker_query_error(int fd, uint32_t query_id, const char *error_msg) {
    uint8_t op = OP_WORKER_QUERY_ERROR;
    uint32_t query_id_net = htonl(query_id);
    
    // Longitud del mensaje de error
    uint32_t msg_len = error_msg ? strlen(error_msg) : 0;
    uint32_t msg_len_net = htonl(msg_len);

    if (send(fd, &op, 1, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &query_id_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    if (send(fd, &msg_len_net, 4, MSG_NOSIGNAL) <= 0) return -1;
    
    if (msg_len > 0) {
        if (send(fd, error_msg, msg_len, MSG_NOSIGNAL) <= 0) return -1;
    }

    return 0;
}

int protocol_recv_worker_query_error(int fd, uint32_t *out_query_id, char **out_error_msg) {
    // Leer opcode
    uint8_t opcode = 0;
    if (recv(fd, &opcode, sizeof(uint8_t), MSG_WAITALL) != sizeof(uint8_t))
        return -1;
    
    if (opcode != OP_WORKER_QUERY_ERROR)
        return -2;
    
    // Leer query_id
    uint32_t query_id_net;
    if (recv(fd, &query_id_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t))
        return -3;
    
    *out_query_id = ntohl(query_id_net);
    
    // Leer longitud del mensaje de error
    uint32_t msg_len_net;
    if (recv(fd, &msg_len_net, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t))
        return -4;
    
    uint32_t msg_len = ntohl(msg_len_net);
    
    // Leer mensaje de error
    if (msg_len > 0) {
        char *error_msg = malloc(msg_len + 1);
        if (!error_msg) return -5;
        
        if (recv(fd, error_msg, msg_len, MSG_WAITALL) != (ssize_t)msg_len) {
            free(error_msg);
            return -6;
        }
        
        error_msg[msg_len] = '\0';
        *out_error_msg = error_msg;
    } else {
        *out_error_msg = NULL;
    }
    
    return 0;
}

