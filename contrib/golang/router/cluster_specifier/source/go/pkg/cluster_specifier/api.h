#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

int envoyGoClusterSpecifierGetHeader(unsigned long long header_ptr, void* key, void* value);
void envoyGoClusterSpecifierLogError(unsigned long long plugin_ptr, void* msg);

#ifdef __cplusplus
} // extern "C"
#endif
