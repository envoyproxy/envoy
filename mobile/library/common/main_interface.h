#pragma once

// NOLINT(namespace-envoy)

/**
 * External entrypoint for library.
 */
#ifdef __cplusplus
extern "C" int run_envoy(const char* config);
#else
int run_envoy(const char* config);
#endif
