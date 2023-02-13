#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void envoyGoClusterSpecifierGetHeader(unsigned long long headerPtr, void* key, void* value);
void envoyGoClusterSpecifierCopyHeaders(unsigned long long headerPtr, void* strs, void* buf);
void envoyGoClusterSpecifierSetHeader(unsigned long long headerPtr, void* key, void* value);
void envoyGoClusterSpecifierRemoveHeader(unsigned long long headerPtr, void* key);

#ifdef __cplusplus
} // extern "C"
#endif
