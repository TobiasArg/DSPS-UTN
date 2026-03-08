// /*
//  * Este archivo ya no se usa.
//  * Toda la funcionalidad ha sido migrada a storage_operations.c
//  * 
//  * El sistema ahora usa un enfoque unificado en:
//  * - FASE 1: Estructura física 
//  * - FASE 2: Bitmap allocation
//  * - FASE 3: Hard links + Copy-on-Write
//  * - FASE 4: MD5 deduplication
//  */

//     void handle_write_file_tag(const char* punto_montaje, const char* tag, const void* data, uint32_t size) {
//             if (!tag || strlen(tag) == 0) {
//             log_error(STORAGE_LOG, "Tag inválido recibido en WRITE");s
//             return;
//         }
//         char files_path[512];
//         snprintf(files_path, sizeof(files_path), "%s/Files", punto_montaje);

//         // Calcular hash MD5 del contenido
//         char* md5_str = calcular_md5(data, size);
//         log_info(STORAGE_LOG, "Hash MD5 del contenido: %s", md5_str);

//         // Ruta del archivo basado en MD5 (para evitar duplicados)
//         char md5_path[512];
//         snprintf(md5_path, sizeof(md5_path), "%s/%s.bin", files_path, md5_str);

//         // Verificar si ya existe
//         if (access(md5_path, F_OK) == 0) {
//             log_info(STORAGE_LOG, "Archivo con mismo MD5 ya existe, no se duplica.");
//         } else {
//             FILE* f = fopen(md5_path, "wb");
//             if (!f) {
//                 log_error(STORAGE_LOG, "Error creando archivo '%s': %s", md5_path, strerror(errno));
//                 free(md5_str);
//                 return;
//             }
//             fwrite(data, 1, size, f);
//             fclose(f);
//             log_info(STORAGE_LOG, "Archivo nuevo creado con hash %s", md5_str);
//         }

//         // Ahora asociar el tag con ese MD5
//         char tag_link[512];
//         snprintf(tag_link, sizeof(tag_link), "%s/%s.txt", files_path, tag);

//         FILE* link = fopen(tag_link, "w");
//         if (!link) {
//             log_error(STORAGE_LOG, "Error creando archivo de tag '%s': %s", tag_link, strerror(errno));
//             free(md5_str);
//             return;
//         }
//         fprintf(link, "%s\n", md5_str);
//         fclose(link);

//         free(md5_str);
//     }

// /* 
//     Lee datos reales del archivo del tag dentro del FS.
//     Devuelve un buffer dinámico que debe liberarse con free().
// */
//     void* handle_read_file_tag(const char* punto_montaje, const char* tag, uint32_t offset, uint32_t size) {
//         if (!tag || strlen(tag) == 0) {
//             log_error(STORAGE_LOG, "Tag inválido recibido en read");
//             return NULL;
//         }
        
//         if (!storage_ctx) {
//             log_error(STORAGE_LOG, "Storage context no inicializado");
//             return NULL;
//         }

//         // Parsear el formato FILE_NAME:TAG_NAME
//         char* tag_copy = strdup(tag);
//         char* colon = strchr(tag_copy, ':');
//         if (!colon) {
//             log_error(STORAGE_LOG, "Formato de tag inválido. Esperado: FILE_NAME:TAG_NAME");
//             free(tag_copy);
//             return NULL;
//         }

//         *colon = '\0';
//         char* file_name = tag_copy;
//         char* tag_name = colon + 1;

//         log_info(STORAGE_LOG, "Intentando leer %s/%s", file_name, tag_name);
        
//         // Calcular bloque lógico y offset dentro del bloque
//         uint32_t block_size = storage_ctx->block_size;
//         uint32_t logical_block = offset / block_size;
//         uint32_t block_offset = offset % block_size;

//         // Leer el bloque usando nuestro sistema
//         void* block_buffer = malloc(block_size);
//         if (!block_buffer) {
//             log_error(STORAGE_LOG, "No se pudo reservar memoria para lectura de bloque");
//             free(tag_copy);
//             return NULL;
//         }

//         if (!storage_read_block(storage_ctx, file_name, tag_name, logical_block, block_buffer)) {
//             log_error(STORAGE_LOG, "Error leyendo bloque %u de %s/%s", logical_block, file_name, tag_name);
//             free(block_buffer);
//             free(tag_copy);
//             return NULL;
//         }

//         // Crear buffer de respuesta con el tamaño solicitado
//         void* buffer = malloc(size);
//         if (!buffer) {
//             log_error(STORAGE_LOG, "No se pudo reservar memoria para respuesta");
//             free(block_buffer);
//             free(tag_copy);
//             return NULL;
//         }

//         // Copiar datos desde el offset correcto
//         memcpy(buffer, (char*)block_buffer + block_offset, size);

//         free(block_buffer);
//         free(tag_copy);
        
//         log_info(STORAGE_LOG, "Lectura exitosa: %u bytes desde %s/%s", size, file_name, tag_name);
//         return buffer;
//     }


//     char* calcular_md5(const void* data, size_t size) {
//         // Usar crypto_md5() de commons (obligatorio según enunciado)
//         char* md5_str = string_from_format("%s", crypto_md5((void*)data, size));
//         return md5_str; // liberar luego con free()
//     }

// /*
//     Elimina un archivo File:Tag del sistema de archivos.
//     Elimina el archivo .txt que contiene el MD5 asociado al tag.
// */
// void handle_delete_file_tag(const char* punto_montaje, const char* file_tag) {
//     if (!file_tag || strlen(file_tag) == 0) {
//         log_error(STORAGE_LOG, "File:Tag inválido recibido en DELETE");
//         return;
//     }
    
//     char files_path[512];
//     snprintf(files_path, sizeof(files_path), "%s/Files", punto_montaje);
    
//     // Construir ruta del archivo tag
//     char tag_path[512];
//     snprintf(tag_path, sizeof(tag_path), "%s/%s.txt", files_path, file_tag);
    
//     // Verificar si el archivo existe
//     if (access(tag_path, F_OK) != 0) {
//         log_warning(STORAGE_LOG, "El archivo '%s' no existe, no se puede eliminar", file_tag);
//         return;
//     }
    
//     // Eliminar el archivo
//     if (unlink(tag_path) == 0) {
//         log_info(STORAGE_LOG, "Archivo '%s' eliminado exitosamente", file_tag);
//     } else {
//         log_error(STORAGE_LOG, "Error al eliminar archivo '%s': %s", file_tag, strerror(errno));
//     }
// }

