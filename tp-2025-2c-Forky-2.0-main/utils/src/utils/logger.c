#include "logger.h"

static const char *_logger_operation_name(t_log_operation op);
static void _logger_format_metadata(t_metadata *metadata, char *buf, size_t size);
static void _logger_write(t_log *logger, t_log_level level, const char *msg, t_metadata *metadata);

t_log* logger_create(char* file, char* name, t_log_level log_level)
{
    if (!file || !name)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m 'file' o 'name' nulo al crear logger %s@%d%s", COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    t_log* logger = log_create(file, name, true, log_level);
    if (!logger)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m No se puede crear el logger para el módulo '%s' %s@%s:%d%s", COLOR_ERROR, name, COLOR_LIGHT_GRAY, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    return logger;
}

void logger_info(t_log *logger, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en logger_info %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    _logger_write(logger, LOG_LEVEL_INFO, msg, metadata);
}

void logger_debug(t_log *logger, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en logger_debug %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    _logger_write(logger, LOG_LEVEL_DEBUG, msg, metadata);
}

void logger_warning(t_log *logger, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en logger_warning %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    _logger_write(logger, LOG_LEVEL_WARNING, msg, metadata);
}

void logger_trace(t_log *logger, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en logger_trace %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    _logger_write(logger, LOG_LEVEL_TRACE, msg, metadata);
}

void logger_error(t_log *logger, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en logger_error %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }
    _logger_write(logger, LOG_LEVEL_ERROR, msg, metadata);
}

static const char *_logger_operation_name(t_log_operation op)
{
    switch (op)
    {
    case LOG_SOCKET: return "SCKT";
    case LOG_THREAD: return "HILO";
    case LOG_PROCESS: return "PROC";
    case LOG_PLANNING: return "PLAN";
    case LOG_MEMORY: return "MEMO";
    case LOG_CORE: return "CORE";
    default: return "UNKN";
    }
}

static void _logger_format_metadata(t_metadata *metadata, char *buf, size_t size)
{
    if (!buf)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m buffer nulo en _logger_format_metadata %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    if (!metadata || !metadata->head)
    {
        buf[0] = '\0';
        return;
    }

    char *cursor = buf;
    size_t remaining = size;

    for (t_metadata_node *node = metadata->head; node && remaining > 1; node = node->next)
    {
        if (!node->key || !node->value)
        {
            error_show("%s\x1b[31m[[Abortar!]]\x1b[0m node key o value nulo en _logger_format_metadata %s@%d%s",
                       COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
            abort();
        }

        int written = snprintf(cursor, remaining, " %s%s=%s%s",
                               COLOR_LIGHT_GRAY, node->key, COLOR_RESET, node->value);
        if (written < 0 || (size_t)written >= remaining)
            break;

        cursor += written;
        remaining -= written;
    }
}

static void _logger_write(t_log *logger, t_log_level level, const char *msg, t_metadata *metadata)
{
    if (!logger || !msg)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m logger o msg nulo en _logger_write %s@%d%s",
                   COLOR_ERROR, __FILE__, __LINE__, COLOR_RESET);
        abort();
    }

    if (!metadata)
    {
        error_show("%s\x1b[31m[[Abortar!]]\x1b[0m metadata nulo en _logger_write %s%s@%d",
                   COLOR_ERROR, COLOR_RESET, __FILE__, __LINE__);
        abort();
    }

    char meta_buf[512];
    _logger_format_metadata(metadata, meta_buf, sizeof(meta_buf));

    char *final_msg;
    if (string_is_empty(meta_buf))
        final_msg = string_from_format("(%s): %s", _logger_operation_name(metadata->operation), msg);
    else
        final_msg = string_from_format("(%s): %s%s", _logger_operation_name(metadata->operation), msg, meta_buf);

    switch (level)
    {

    case LOG_LEVEL_INFO:    log_info(logger, "      %s", final_msg); break;
    case LOG_LEVEL_DEBUG:   log_debug(logger, "     %s", final_msg); break;
    case LOG_LEVEL_WARNING: log_warning(logger, "   %s", final_msg); break;
    case LOG_LEVEL_TRACE:   log_trace(logger, "     %s", final_msg); break;
    case LOG_LEVEL_ERROR:
    {
        const char *file_str = metadata->file ? metadata->file : "?";
        log_error(logger, "  %s %s@%s:%d%s",
                  final_msg, COLOR_LIGHT_GRAY, file_str, metadata->line, COLOR_RESET);
        break;
    }
    default: break;
    }

    free(final_msg);
}
