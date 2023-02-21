#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void envoyGoClusterSpecifierGetHeader(unsigned long long headerPtr, void* key, void* value);
void envoyGoClusterSpecifierLogError(unsigned long long pluginPtr, void* msg);

#ifdef __cplusplus
} // extern "C"
#endif
