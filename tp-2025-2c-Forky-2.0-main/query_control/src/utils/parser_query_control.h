/*  parse_query_content.h  */
#ifndef PARSE_QUERY_CONTENT_H
#define PARSE_QUERY_CONTENT_H

#include <stddef.h>

/* Reemplaza '\n' por '|' al copiar src -> dst */
size_t parse_query_content(char *dst, size_t dst_size, const char *src);

#endif /* PARSE_QUERY_CONTENT_H */