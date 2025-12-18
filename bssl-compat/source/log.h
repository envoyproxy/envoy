/* Placeholder for a proper logging implementation */
#ifndef _BSSL_COMPAT_LOG_H_
#define _BSSL_COMPAT_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
  BSSL_COMPAT_LOG_DEBUG,
  BSSL_COMPAT_LOG_INFO,
  BSSL_COMPAT_LOG_WARN,
  BSSL_COMPAT_LOG_ERROR,
  BSSL_COMPAT_LOG_FATAL
};

void bssl_compat_log(int level, const char *file, int line, const char *fmt, ...);

#define bssl_compat_debug(...) bssl_compat_log(BSSL_COMPAT_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define bssl_compat_info(...)  bssl_compat_log(BSSL_COMPAT_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define bssl_compat_warn(...)  bssl_compat_log(BSSL_COMPAT_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define bssl_compat_error(...) bssl_compat_log(BSSL_COMPAT_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define bssl_compat_fatal(...) bssl_compat_log(BSSL_COMPAT_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
