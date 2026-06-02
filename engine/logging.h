#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#define LOG_INFO(fmt, ...)    fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_PROGRESS(fmt, ...) fprintf(stderr, "[PROGRESS] " fmt "\n", ##__VA_ARGS__)

#endif
