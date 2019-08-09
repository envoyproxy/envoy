#include "library/common/include/c_types.h"

// NOLINT(namespace-envoy)

void envoy_noop_release(void* context) { (void)context; }

void release_envoy_headers(envoy_headers headers) {
  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    envoy_header header = headers.headers[i];
    header.key.release(header.key.context);
    header.value.release(header.value.context);
  }
  free(headers.headers);
}

const envoy_data envoy_nodata = {0, NULL, envoy_noop_release, NULL};
