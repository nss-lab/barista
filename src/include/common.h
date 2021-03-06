/*
 * Copyright 2015-2019 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include "mac2int.h"
#include "ip2int.h"

#include "database.h"
#include <jansson.h>
#include "libcli.h"
#include <mysql.h>
#include <zmq.h>

#include "type.h"

/* ==== Default ==== */

#define __CONF_ARGC 16
#define __CONF_SHORT_LEN 16
#define __CONF_WORD_LEN 256
#define __CONF_STR_LEN 1024
#define __CONF_LONG_STR_LEN 2048
#define __DEFAULT_TABLE_SIZE 65536

/* ==== Barista NOS ==== */

/** \brief The default CLI access address */
#define __CLI_HOST "127.0.0.1"

/** \brief The default CLI port */
#define __CLI_PORT 8000

/** \brief The maximum number of CLI connections */
#define __CLI_MAX_CONNECTIONS 10

/** \brief The default database access address */
#define __DB_HOST "127.0.0.1"

/** \brief The default database port */
#define __DB_PORT 3306

/** \brief The default pulling address for external events */
#define __EXT_COMP_PULL_ADDR "tcp://127.0.0.1:5001"

/** \brief The default replying address for external events */
#define __EXT_COMP_REPLY_ADDR "tcp://127.0.0.1:5002"

/** \brief The default pulling address for external app events */
#define __EXT_APP_PULL_ADDR "tcp://127.0.0.1:6001"

/** \brief The default replying address for external app events */
#define __EXT_APP_REPLY_ADDR "tcp://127.0.0.1:6002"

/** \brief The number of characters to be used to generate IDs */
#define __HASHING_NAME_LENGTH 8

/** \brief The default component configuration file */
#define DEFAULT_COMPNT_CONFIG_FILE "config/components.conf"

/** \brief The default component configuration file */
#define BASE_COMPNT_CONFIG_FILE "config/components.base"

/** \brief The default application configuration file */
#define DEFAULT_APP_CONFIG_FILE "config/applications.conf"

/** \brief The default application configuration file */
#define BASE_APP_CONFIG_FILE "config/applications.base"

/** \brief The default application role file */
#define DEFAULT_APP_ROLE_FILE "config/app_events.role"

/** \brief The default operator-defined policy file */
#define DEFAULT_ODP_FILE "config/operator-defined.policy"

/* ==== Wrappers ==== */

/** \brief Font color */
/* @{ */
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
/* @} */

/** \brief TRUE, FALSE */
/** @{ */
#define TRUE 1
#define FALSE 0
/** @} */

/** \brief Min function */
#define MIN(a,b) (((a)<(b))?(a):(b))

/** \brief Max function */
#define MAX(a,b) (((a)>(b))?(a):(b))

/** \brief Timer */
#define waitsec(sec, nsec) \
{ \
    struct timespec req; \
    req.tv_sec = sec; \
    req.tv_nsec = nsec; \
    nanosleep(&req, NULL); \
}

/** \brief Activation code for the main function of each component */
#define activate() \
{ \
    *activated = TRUE; \
    waitsec(0, 1000); \
}

/** \brief Deactivation code for the cleanup function of each component */
#define deactivate() \
{ \
    *activated = FALSE; \
    waitsec(0, 1000); \
}

/** \brief Functions to allocate a space */
#define MALLOC(x) malloc(x)
#define CALLOC(x, y) calloc(x, y)

/** \brief Function to release a space if the space is valid */
#define FREE(x) \
{ \
    if (x) { \
        free(x); \
        x = NULL; \
    } \
}

/** \brief Functions to change the byte orders of 64-bit values */
/* @{ */
#ifdef WORDS_BIGENDIAN
#define htonll(x) (x)
#define ntohll(x) (x)
#else
#define htonll(x) ((((uint64_t)htonl(x)) <<32) + htonl(x>> 32))
#define ntohll(x) ((((uint64_t)ntohl(x)) <<32) + ntohl(x>> 32))
#endif
/* @} */

/** \brief Functions to print messages through the CLI */
/* @{ */
#define cli_bufcls(buf) memset(buf, 0, sizeof(buf))
#define cli_buffer(buf, format, args...) sprintf(&buf[strlen(buf)], format, ##args)
#define cli_bufprt(cli, buf) cli_print(cli, "%s", buf)
/* @} */

/** \brief Functions for debug and error messages */
/* @{ */
//#define __ENABLE_DEBUG
#ifdef __ENABLE_DEBUG
#define DEBUG(format, args...) ({ printf("%s: " format, __FUNCTION__, ##args); fflush(stdout); })
#define PRINTF(format, args...) ({ printf("%s: " format, __FUNCTION__, ##args); fflush(stdout); })
#define PERROR(msg) fprintf(stdout, "\x1b[31m[%s:%d] %s: %s\x1b[0m\n", __FUNCTION__, __LINE__, (msg), strerror(errno))
#else /* !__ENABLE_DEBUG */
#define DEBUG(format, args...) (void)0
#define PRINTF(format, args...) ({ printf(format, ##args); fflush(stdout); })
#define PERROR(msg) fprintf(stdout, "\x1b[31m[%s:%d] %s: %s\x1b[0m\n", __FUNCTION__, __LINE__, (msg), strerror(errno))
#endif /* !__ENABLE_DEBUG */
/* @} */

/** \brief Function for event messages */
/* @{ */
#ifdef __ENABLE_EVENT_DEBUG
#define PRINT_EV(format, args...) ({ printf("%s: " format, __FUNCTION__, ##args); fflush(stdout); })
#else /* !__ENABLE_EVENT_DEBUG */
#define PRINT_EV(format, args...) (void)0
#endif /* !__ENABLE_EVENT_DEBUG */
/* @} */

/** \brief Wrappers for component-side logging */
/* @{ */
#define LOG_FATAL(id, format, args...) ev_log_fatal(id, format, ##args)
#define LOG_ERROR(id, format, args...) ev_log_error(id, format, ##args)
#define LOG_WARN(id, format, args...)  ev_log_warn(id, format, ##args)
#define LOG_INFO(id, format, args...)  ev_log_info(id, format, ##args)
#define LOG_DEBUG(id, format, args...) ev_log_debug(id, format, ##args)
/* @} */

/** \brief Wrappers for application-side logging */
/* @{ */
#define ALOG_FATAL(id, format, args...) av_log_fatal(id, format, ##args)
#define ALOG_ERROR(id, format, args...) av_log_error(id, format, ##args)
#define ALOG_WARN(id, format, args...)  av_log_warn(id, format, ##args)
#define ALOG_INFO(id, format, args...)  av_log_info(id, format, ##args)
#define ALOG_DEBUG(id, format, args...) av_log_debug(id, format, ##args)
/* @} */
