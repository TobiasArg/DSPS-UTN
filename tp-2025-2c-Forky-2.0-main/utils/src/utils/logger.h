#ifndef LOGGER_H
#define LOGGER_H

#include "metadata.h"
#include "include.h"

extern t_log *logger;

t_log* logger_create(char* file, char* name, t_log_level log_level);
void logger_info   (t_log* logger, const char* msg, t_metadata* metadata);
void logger_debug  (t_log* logger, const char* msg, t_metadata* metadata);
void logger_warning(t_log* logger, const char* msg, t_metadata* metadata);
void logger_error  (t_log* logger, const char* msg, t_metadata* metadata);
void logger_trace  (t_log* logger, const char* msg, t_metadata* metadata);

#endif /* LOGGER_H */
