#include "../main.h"

t_config_master *cargar_configuracion(char *path_config)
{
    t_config *config = config_create(path_config);
    if (config == NULL)
    {
        return NULL;
    }

    t_config_master *config_m = malloc(sizeof(t_config_master));
    if (!config_m)
        abort();

    config_m->algoritmo_planning = string_duplicate(config_get_string_value(config, "ALGORITMO_PLANIFICACION"));
    config_m->ip_master = string_duplicate(config_get_string_value(config, "IP_ESCUCHA"));
    config_m->log_level = string_duplicate(config_get_string_value(config, "LOG_LEVEL"));
    config_m->puerto_master = config_get_int_value(config, "PUERTO_ESCUCHA");
    config_m->tiempo_aging = config_get_int_value(config, "TIEMPO_AGING");

    config_destroy(config);
    return config_m;
}