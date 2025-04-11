#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> // NOLINT(modernize-deprecated-headers)

uint64_t envoyGoClusterSpecifierGetNumHeaders(unsigned long long header_ptr);
uint64_t envoyGoClusterSpecifierGetHeadersByteSize(unsigned long long header_ptr);
int envoyGoClusterSpecifierGetHeader(unsigned long long header_ptr, void* key, void* value);
void envoyGoClusterSpecifierLogError(unsigned long long plugin_ptr, void* msg);
void envoyGoClusterSpecifierGetAllHeaders(unsigned long long header_ptr, void* strs, void* buf);

#ifdef __cplusplus
} // extern "C"
#endif
