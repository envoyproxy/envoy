#pragma once

#import "library/common/types/c_types.h"
#import "library/objective-c/EnvoyKeyValueStore.h"

/// Save a value to the key value store that's passed as an opaque context.
void ios_kv_store_save(envoy_data native_key, envoy_data native_value, const void* context);

/// Read a value from the key value store that's passed as an opaque context.
envoy_data ios_kv_store_read(envoy_data native_key, const void* context);

/// Remove a value from the key value store that's passed as an opaque context.
void ios_kv_store_remove(envoy_data native_key, const void* context);
