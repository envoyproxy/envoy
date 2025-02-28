#pragma once
#pragma GCC diagnostic ignored "-Wold-style-cast"

#ifdef __cplusplus
#include "contrib/golang/common/dso/api.h"
struct httpDestroyableConfig : httpConfig {
    int destroyed;
} ;
extern "C" {
#else
#include "contrib/golang/common/go/api/api.h"
typedef struct {
    httpConfig c;
    int destroyed;
} httpDestroyableConfig;
#endif

void envoyGoConfigDestroy(void* c) {
    httpDestroyableConfig* dc = (httpDestroyableConfig*)(c);
    dc->destroyed = 1;
};

#ifdef __cplusplus
} // extern "C"
#endif
