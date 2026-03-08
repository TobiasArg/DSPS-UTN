#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#define OP_CREATE_FILE_TAG_20

/* ------------------------------------------------------------------ */
/*  Handshake básico (Worker ↔ Storage)                               */
/* ------------------------------------------------------------------ */
enum {
    OP_WORKER_HANDSHAKE      = 0x01,
    OP_STORAGE_HANDSHAKE_OK  = 0x02
};

/* ------------------------------------------------------------------ */
/*  Worker -> Storage: operaciones sobre archivos                     */
/* ------------------------------------------------------------------ */
enum {
    OP_STORAGE_CREATE    = 0x50,
    OP_STORAGE_TRUNCATE  = 0x51,
    OP_STORAGE_WRITE     = 0x52,
    OP_STORAGE_READ      = 0x53,
    OP_STORAGE_FLUSH     = 0x54,
    OP_STORAGE_COMMIT    = 0x55,
    OP_STORAGE_TAG       = 0x30,
    OP_STORAGE_DELETE    = 0x32,
    OP_STORAGE_END       = 0x59   /* para cerrar la conexión */
};

/* ------------------------------------------------------------------ */
/*  Respuestas del Storage                                            */
/* ------------------------------------------------------------------ */
enum {
    ST_OK    = 0x00,
    ST_ERROR = 0xFF
};

/* ------------------------------------------------------------------ */
/*  Master - QueryControl                                             */
/* ------------------------------------------------------------------ */
enum {
    OP_QUERY_SUBMIT          = 0x10,
    OP_QUERY_CONFIRM         = 0x11,
    OP_QUERY_RESULT          = 0x12,
    OP_QUERY_READ_MESSAGE    = 0x13
};

/* ------------------------------------------------------------------ */
/*  Master - Worker                                                   */
/* ------------------------------------------------------------------ */
enum {
    OP_MASTER_WORKER_REGISTER     = 0x20,
    OP_MASTER_WORKER_REGISTER_ACK = 0x21,
    OP_MASTER_QUERY_ASSIGN        = 0x22,
    OP_MASTER_QUERY_COMPLETE      = 0x23,
    OP_MASTER_SEND_PATH           = 0x24,
    OP_MASTER_EVICT               = 0x14,   /* Master solicita desalojo */
    OP_WORKER_EVICT_ACK           = 0x15,   /* Worker confirma desalojo */
    OP_WORKER_READ_RESULT         = 0x16,   /* Worker envía resultado de READ al Master */
    OP_WORKER_QUERY_FINISHED      = 0x17,   /* Worker notifica finalización exitosa de query */
    OP_WORKER_QUERY_ERROR         = 0x18    /* Worker notifica error en query (falló READ, WRITE, etc.) */
};

/* ------------------------------------------------------------------ */
/*  Utilidades de red                                                 */
/* ------------------------------------------------------------------ */
size_t recv_all(int fd, void *buf, size_t len);
size_t send_all(int fd, const void *buf, size_t len);

/* ------------------------------------------------------------------ */
/*  Worker ↔ Storage                                                  */
/* ------------------------------------------------------------------ */
int protocol_send_worker_handshake(int fd, uint32_t worker_id);
int protocol_recv_storage_handshake_ok(int fd, uint32_t *out_block_size);

int protocol_send_worker_create(int fd, const char *file, const char *tag, uint32_t size, uint32_t query_id);
int protocol_recv_worker_create(int fd, char **out_file, char **out_tag, uint32_t *out_size, uint32_t *out_query_id);

int protocol_send_storage_delete(int fd, const char* file_tag, uint32_t query_id);
int protocol_send_storage_tag(int fd, const char *origen, const char *destino);

int protocol_recv_worker_handshake(int fd, uint32_t *out_worker_id);
int protocol_send_storage_handshake_ok(int fd, uint32_t block_size);
int protocol_recv_storage_response(int fd);   /* lee ST_OK / ST_ERROR */

/* ------------------------------------------------------------------ */
/*  Master - QueryControl                                             */
/* ------------------------------------------------------------------ */
int protocol_recv_query_submit(int fd, char **out_query_path, uint32_t *out_priority);
int protocol_send_query_confirm(int fd, uint32_t query_id);
int protocol_send_query_result(int fd, uint32_t query_id, const char *result);
int protocol_recv_query_result(int fd, uint32_t *out_query_id, char **out_result);
int protocol_send_query_read_message(int fd, uint32_t query_id, const char *read_data);
int protocol_recv_query_read_message(int fd, uint32_t *out_query_id, char **out_read_data);

/* ------------------------------------------------------------------ */
/*  Master - Worker                                                   */
/* ------------------------------------------------------------------ */
int protocol_recv_worker_register(int fd, uint32_t *out_worker_id);
int protocol_send_worker_register_ack(int fd, uint32_t worker_id);
int protocol_recv_worker_register_ack(int fd, uint32_t *out_worker_id);
int protocol_send_query_assign(int fd, uint32_t query_id, char *query_path);
int protocol_recv_query_complete(int fd, uint32_t *out_query_id, char **out_result);
int protocol_send_worker_register_master(int fd, uint32_t worker_id);

int protocol_send_master_path(int fd, uint32_t query_id, const char *path, uint32_t pc);
int protocol_recv_master_path(int fd, uint32_t* out_query_id, char **out_path, uint32_t *out_pc);
//AGREGUE LAS ULT VARIABLES
int protocol_send_master_evict(int fd, uint32_t query_id, uint32_t version);
int protocol_recv_master_evict(int fd, uint32_t *out_query_id, uint32_t *out_version);
int protocol_send_worker_evict_ack(int fd, uint32_t query_id, uint32_t pc, uint32_t version);
int protocol_recv_worker_evict_ack(int fd, uint32_t *out_query_id, uint32_t *out_pc, uint32_t *out_version);
int protocol_recv_worker_evict_ack_timeout(int fd, uint32_t *out_query_id, uint32_t *out_pc, int timeout_ms, uint32_t *out_version );

int protocol_send_worker_read_result(int fd, uint32_t query_id, const char *file_tag, const void *data, uint32_t size);
int protocol_recv_worker_read_result(int fd, uint32_t *out_query_id, char **out_file_tag, char **out_data, uint32_t *out_size);

int protocol_send_worker_query_finished(int fd, uint32_t query_id);
int protocol_recv_worker_query_finished(int fd, uint32_t *out_query_id);

int protocol_send_worker_query_error(int fd, uint32_t query_id, const char *error_msg);
int protocol_recv_worker_query_error(int fd, uint32_t *out_query_id, char **out_error_msg);

int protocol_send_worker_commit(int fd, const char *file, const char *tag, uint32_t query_id);
int protocol_recv_worker_commit(int fd, char **out_file, char **out_tag);
int protocol_send_worker_truncate(int fd, const char *file, const char *tag, uint32_t new_size, uint32_t query_id);
int protocol_recv_worker_truncate(int fd, char **out_file, char **out_tag, uint32_t *out_new_size);

int protocol_send_worker_tag(int fd, const char *file_orig, const char *tag_orig, const char *file_dst,  const char *tag_dst, uint32_t query_id);
int protocol_recv_worker_tag(int fd, char **out_file_orig, char **out_tag_orig, char **out_file_dst,  char **out_tag_dst);

size_t recv_all(int fd, void *buf, size_t len);
size_t recv_all_timeout(int fd, void *buf, size_t len, int timeout_ms);
size_t send_all(int fd, const void *buf, size_t len);
