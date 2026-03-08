#ifndef METADATA_H
#define METADATA_H

#include <stdlib.h>
#include <string.h>

typedef enum {
    LOG_SOCKET,
    LOG_THREAD,
    LOG_PROCESS,
    LOG_PLANNING,
    LOG_MEMORY,
    LOG_CORE
} t_log_operation;

typedef struct t_metadata_node {
    char* key;
    char* value;
    struct t_metadata_node* next;
} t_metadata_node;

typedef struct {
    t_log_operation operation;
    const char* file;
    int line;
    t_metadata_node* head;
} t_metadata;

t_metadata* metadata_create_full(t_log_operation op, const char* file, int line);
#define metadata_create(op) metadata_create_full(op, __FILE__, __LINE__)

void metadata_add(t_metadata* metadata, const char* key, const char* value);
void metadata_clear(t_metadata* metadata);
int metadata_replace(t_metadata* metadata, const char* key, const char* value);
int metadata_add_unique(t_metadata* metadata, const char* key, const char* value);
void metadata_destroy(t_metadata* metadata);

#endif /* METADATA_H */
