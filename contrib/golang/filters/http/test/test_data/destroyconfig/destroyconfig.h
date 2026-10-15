#pragma once
// NOLINT(namespace-envoy)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "api.h"

#ifdef __cplusplus
// NOLINTNEXTLINE(readability-identifier-naming)
struct httpDestroyableConfig : httpConfig {
  int destroyed;
};
extern "C" {
#else
typedef struct {
  httpConfig c;
  int destroyed;
  // NOLINTNEXTLINE(readability-identifier-naming)
} httpDestroyableConfig;
#endif

void envoyGoConfigDestroy(void* c) {
  httpDestroyableConfig* dc = (httpDestroyableConfig*)(c);
  dc->destroyed = 1;
};

#ifdef __cplusplus
} // extern "C"
#endif
