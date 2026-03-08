#include "parser_query_control.h"

size_t parse_query_content(char *dst, size_t dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0)
        return (size_t)-1;

    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_size; ++i)
        dst[j++] = (src[i] == '\n') ? '|' : src[i];

    dst[j] = '\0';
    return j;
}