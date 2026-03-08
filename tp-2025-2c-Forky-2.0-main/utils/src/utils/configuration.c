#include "configuration.h"
#include <stdlib.h>
#include <stdio.h>

static inline void _log_and_metadata(t_log *logger, const char *key, const char *repr, const char *type)
{
    if (!logger)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger nulo en _log_and_metadata %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    t_metadata *meta = metadata_create(LOG_CORE);
    metadata_add(meta, "val", repr ? string_from_format("%s", repr) : string_from_format("NULL"));
    metadata_add(meta, "param", (char *)key);
    metadata_add(meta, "type", (char *)type);

    if (repr)
        logger_info(logger, "Configuración obtenida.", meta);
    else
        logger_warning(logger, "Parámetro no encontrado en la configuración. Retorna NULL.", meta);

    metadata_destroy(meta);
}

t_config *configuration_read(const char *path)
{
    if (!path)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m path nulo en configuration_read %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    return config_create((char *)path);
}

void configuration_destroy(t_config *cfg)
{
    if (cfg)
        config_destroy(cfg);
}

bool configuration_exist_property(t_config *cfg, const char *key)
{
    if (!cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m cfg o key nulo en configuration_exist_property %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    return config_has_property(cfg, (char *)key);
}

const char *configuration_get_string(t_log *logger, t_config *cfg, const char *key)
{
    if (!logger || !cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger, cfg o key nulo en configuration_get_string %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    char *value = config_get_string_value(cfg, (char *)key);
    _log_and_metadata(logger, key, value, "string");
    return value ? value : NULL;
}

int configuration_get_int(t_log *logger, t_config *cfg, const char *key)
{
    if (!logger || !cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger, cfg o key nulo en configuration_get_int %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    int value = config_get_int_value(cfg, (char *)key);
    char buf[32];
    snprintf(buf, sizeof buf, "%d", value);
    _log_and_metadata(logger, key, buf, "int");
    return value;
}

long configuration_get_long(t_log *logger, t_config *cfg, const char *key)
{
    if (!logger || !cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger, cfg o key nulo en configuration_get_long %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    long value = config_get_long_value(cfg, (char *)key);
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", value);
    _log_and_metadata(logger, key, buf, "long");
    return value;
}

double configuration_get_double(t_log *logger, t_config *cfg, const char *key)
{
    if (!logger || !cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger, cfg o key nulo en configuration_get_double %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    double value = config_get_double_value(cfg, (char *)key);
    char buf[64];
    snprintf(buf, sizeof buf, "%.2f", value);
    _log_and_metadata(logger, key, buf, "double");
    return value;
}

char **configuration_get_array(t_log *logger, t_config *cfg, const char *key)
{
    if (!logger || !cfg || !key)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger, cfg o key nulo en configuration_get_array %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    char **value = config_get_array_value(cfg, (char *)key);
    _log_and_metadata(logger, key, value ? "array" : NULL, "array");
    return value;
}
