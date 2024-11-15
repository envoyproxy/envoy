#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // function pointers
#endif

/**
 * Function signature for reading value from implementation.
 */
typedef envoy_data (*envoy_kv_store_read_f)(envoy_data key, const void* context);

/**
 * Function signature for saving value to implementation.
 */
typedef void (*envoy_kv_store_save_f)(envoy_data key, envoy_data value, const void* context);

/**
 * Function signature for removing value from implementation.
 */
typedef void (*envoy_kv_store_remove_f)(envoy_data key, const void* context);

#ifdef __cplusplus
} // function pointers
#endif

typedef struct {
  envoy_kv_store_read_f read;
  envoy_kv_store_save_f save;
  envoy_kv_store_remove_f remove;
  const void* context;
} envoy_kv_store;
