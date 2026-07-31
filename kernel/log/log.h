#ifndef KERNEL_LOG_LOG_H
#define KERNEL_LOG_LOG_H

typedef enum
{
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

void log_info(const char *msg);
void log_warn(const char *msg);
void log_error(const char *msg);

void kernel_log(log_level_t lvl, const char *msg);

#endif