#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void envoyGoClusterSpecifierGetNumHeadersAndByteSize(unsigned long long header_ptr,
                                                     void* header_num, void* byte_size);
int envoyGoClusterSpecifierGetHeader(unsigned long long header_ptr, void* key, void* value);
void envoyGoClusterSpecifierLogError(unsigned long long plugin_ptr, void* msg);
void envoyGoClusterSpecifierGetAllHeaders(unsigned long long header_ptr, void* strs, void* buf);

#ifdef __cplusplus
} // extern "C"
#endif
