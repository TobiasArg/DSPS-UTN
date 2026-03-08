#include "../main.h"
#include <commons/config.h>
#include <stdlib.h>
#include <stdio.h>

t_config_storage storage_load_config(const char* cfg_path) {
    t_config_storage cs = {0};
    t_config* cfg = config_create((char*)cfg_path);

    cs.listen_port       = config_get_int_value(cfg, "PUERTO_ESCUCHA");
    cs.mount_point       = config_get_string_value(cfg, "PUNTO_MONTAJE");
    cs.fresh_start       = config_get_int_value(cfg, "FRESH_START");
    cs.retardo_operacion = config_get_int_value(cfg, "RETARDO_OPERACION");
    cs.retardo_acceso    = config_get_int_value(cfg, "RETARDO_ACCESO_BLOQUE");

    // superblock.config
    char sb_path[512]; snprintf(sb_path, sizeof(sb_path), "%s/superblock.config", cs.mount_point);
    t_config* sb = config_create(sb_path);
    cs.block_size = (uint32_t) (sb ? config_get_int_value(sb, "BLOCK_SIZE") : 128);
    if (sb) config_destroy(sb);

    config_destroy(cfg);
    return cs;
}
