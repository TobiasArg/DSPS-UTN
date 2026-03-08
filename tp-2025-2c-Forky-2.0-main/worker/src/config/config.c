#include "../main.h"

t_config_worker *cargar_configuracion(char *path_config)
{
    t_config *config = config_create(path_config);
    if (config == NULL)
    {
        return NULL;
    }

    
    t_config_worker* config_qc = malloc(sizeof(t_config_worker));
    
    config_qc->ip_master = string_duplicate(config_get_string_value(config, "IP_MASTER"));
    config_qc->puerto_master = config_get_int_value(config, "PUERTO_MASTER");
    
    config_qc->ip_storage = string_duplicate(config_get_string_value(config, "IP_STORAGE"));
    config_qc->puerto_storage = config_get_int_value(config, "PUERTO_STORAGE");
    
    config_qc->tam_memoria = config_get_int_value(config, "TAM_MEMORIA");
    config_qc->retardo_memoria = config_get_int_value(config, "RETARDO_MEMORIA");
    
    config_qc->algoritmo_reemplazo = string_duplicate(config_get_string_value(config, "ALGORITMO_REEMPLAZO"));
    config_qc->path_scripts = string_duplicate(config_get_string_value(config, "PATH_QUERIES"));

    config_qc->log_level = string_duplicate(config_get_string_value(config, "LOG_LEVEL"));

    config_destroy(config);
    return config_qc;
}