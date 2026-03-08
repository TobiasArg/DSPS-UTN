#ifndef QUERY_H
#define QUERY_H

#include <utils/include.h>
#include <utils/parameters.h>
#include <utils/logger.h>
#include <utils/metadata.h>
#include <commons/string.h>
#include "./utils/parser_query_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* ip_master;
    int puerto_master;
    char* log_level;
} t_config_query_control;

t_config_query_control* cargar_configuracion(char* path_config);

#endif /* QUERY_H */