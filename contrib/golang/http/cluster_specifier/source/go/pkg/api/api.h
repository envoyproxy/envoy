#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void envoyGoClusterSpecifierGetHeader(void* h, void* key, void* value);
void envoyGoClusterSpecifierCopyHeaders(void* h, void* strs, void* buf);
void envoyGoClusterSpecifierSetHeader(void* h, void* key, void* value);
void envoyGoClusterSpecifierRemoveHeader(void* h, void* key);

#ifdef __cplusplus
} // extern "C"
#endif
