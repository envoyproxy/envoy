#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // function pointers
#endif

typedef envoy_data (*envoy_get_string_f)(const void* context);

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Datatype used to access strings from the platform layer. This accessor is read-only.
 */
// TODO: https://github.com/envoyproxy/envoy-mobile/issues/1192 generalize to access arbitrary
//       types.
typedef struct {
  envoy_get_string_f get_string;
  const void* context;
} envoy_string_accessor;
