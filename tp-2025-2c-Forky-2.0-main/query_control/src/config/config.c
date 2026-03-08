#include "../main.h"

t_config_query_control* cargar_configuracion(char* path_config) {
    t_config* config = config_create(path_config);
    if (config == NULL) {
        return NULL;
    }
    
    t_config_query_control* config_qc = malloc(sizeof(t_config_query_control));
    config_qc->ip_master = string_duplicate(config_get_string_value(config, "IP_MASTER"));
    config_qc->puerto_master = config_get_int_value(config, "PUERTO_MASTER");
    config_qc->log_level = string_duplicate(config_get_string_value(config, "LOG_LEVEL"));
    
    config_destroy(config);
    return config_qc;
}