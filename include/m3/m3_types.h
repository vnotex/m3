#ifndef M3_TYPES_H
#define M3_TYPES_H
#include <stddef.h>
#if defined(M3_SHARED)
# if defined(_WIN32)
#  if defined(M3_BUILD_DLL)
#   define M3_API __declspec(dllexport)
#  else
#   define M3_API __declspec(dllimport)
#  endif
# else
#  define M3_API __attribute__((visibility("default")))
# endif
#else
# define M3_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef enum M3Status {
    M3_OK = 0,
    M3_ERR_INVALID_ARGUMENT = 1,
    M3_ERR_JSON = 2,
    M3_ERR_SCHEMA = 3,
    M3_ERR_NOT_FOUND = 4,
    M3_ERR_ALREADY_EXISTS = 5,
    M3_ERR_INVALID_OPERATION = 6,
    M3_ERR_OUT_OF_MEMORY = 7,
    M3_ERR_INTERNAL = 8
} M3Status;
/* Thread-local diagnostic; valid until the next status-returning call.
 * Success clears it. Free/destroy helpers do not change it. */
M3_API const char *m3_last_error(void);
/* Release only library-owned UTF-8 snapshots. NULL is accepted. */
M3_API void m3_string_free(char *text);
#ifdef __cplusplus
}
#endif
#endif
