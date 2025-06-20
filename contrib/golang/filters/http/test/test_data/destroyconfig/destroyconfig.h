#pragma once
// NOLINT(namespace-envoy)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "api.h"

#ifdef __cplusplus
struct httpDestroyableConfig : httpConfig {
  int destroyed;
};
extern "C" {
#else
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
