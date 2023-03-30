#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registers the Apple proxy resolver.
 */
void register_apple_proxy_resolver(envoy_engine_t engine_handle);

#ifdef __cplusplus
}
#endif
