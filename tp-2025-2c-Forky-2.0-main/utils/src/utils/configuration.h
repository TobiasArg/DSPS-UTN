#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <commons/config.h>
#include "logger.h"
#include "metadata.h"
#include <stddef.h>
#include <stdbool.h>

t_config *configuration_read(const char *ruta);
void configuration_destroy(t_config *cfg);
bool configuration_exist_property(t_config *cfg, const char *clave);

const char *configuration_get_string(t_log *logger, t_config *cfg, const char *clave);
int configuration_get_int(t_log *logger, t_config *cfg, const char *clave);
long configuration_get_long(t_log *logger, t_config *cfg, const char *clave);
double configuration_get_double(t_log *logger, t_config *cfg, const char *clave);
char **configuration_get_array(t_log *logger, t_config *cfg, const char *clave);

#endif /* CONFIGURATION_H */
