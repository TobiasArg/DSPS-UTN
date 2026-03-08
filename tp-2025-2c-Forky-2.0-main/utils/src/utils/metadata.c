#include "metadata.h"
#include "include.h"

static char* _metadata_string_duplicate(const char* s);
static t_metadata_node** _metadata_find_node(t_metadata* m, const char* key);

t_metadata* metadata_create_full(t_log_operation op, const char* file, int line) {
    t_metadata* m = malloc(sizeof(t_metadata));
    if (!m) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m No se pudo asignar metadata %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    m->operation = op;
    m->file      = file;
    m->line      = line;
    m->head      = NULL;

    return m;
}

void metadata_add(t_metadata* m, const char* key, const char* value) {
    if (!m || !key || !value) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m metadata, key o value nulo en metadata_add %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    t_metadata_node* node = malloc(sizeof(t_metadata_node));
    if (!node) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m No se pudo asignar nodo de metadata %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    node->key   = _metadata_string_duplicate(key);
    node->value = _metadata_string_duplicate(value);
    node->next  = m->head;
    m->head     = node;
}

void metadata_destroy(t_metadata* m) {
    if (!m) return;

    t_metadata_node* cur = m->head;
    while (cur) {
        t_metadata_node* next = cur->next;
        free(cur->key);
        free(cur->value);
        free(cur);
        cur = next;
    }
    free(m);
}

void metadata_clear(t_metadata* metadata) {
    if (!metadata) return;

    t_metadata_node* cur = metadata->head;
    while (cur) {
        t_metadata_node* next = cur->next;
        free(cur->key);
        free(cur->value);
        free(cur);
        cur = next;
    }
    metadata->head = NULL;
}

int metadata_replace(t_metadata* metadata, const char* key, const char* value) {
    if (!metadata || !key || !value) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m metadata, key o value nulo en metadata_replace %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    t_metadata_node** node_ptr = _metadata_find_node(metadata, key);
    if (!node_ptr) return -1;

    t_metadata_node* node = *node_ptr;
    free(node->value);
    node->value = _metadata_string_duplicate(value);
    if (!node->value) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m No se pudo duplicar value en metadata_replace %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    return 0;
}

int metadata_add_unique(t_metadata* metadata, const char* key, const char* value) {
    if (!metadata || !key || !value) {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m metadata, key o value nulo en metadata_add_unique %s@%s:%d%s",
                   COLOR_ERROR, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    if (_metadata_find_node(metadata, key)) return -1;

    metadata_add(metadata, key, value);
    return 0;
}

static char* _metadata_string_duplicate(const char* s) {
    if (!s) return NULL;

    size_t len  = strlen(s) + 1;
    char* copy  = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static t_metadata_node** _metadata_find_node(t_metadata* m, const char* key) {
    if (!m || !key) return NULL;

    t_metadata_node** curr = &m->head;
    while (*curr) {
        if (strcmp((*curr)->key, key) == 0)
            return curr;
        curr = &(*curr)->next;
    }
    return NULL;
}
