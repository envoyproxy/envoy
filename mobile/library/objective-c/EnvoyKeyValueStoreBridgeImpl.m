#import "library/objective-c/EnvoyKeyValueStoreBridgeImpl.h"

#import "library/objective-c/EnvoyBridgeUtility.h"

envoy_data ios_kv_store_read(envoy_data native_key, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    id<EnvoyKeyValueStore> keyValueStore = (__bridge id<EnvoyKeyValueStore>)context;
    NSString *key = [[NSString alloc] initWithBytes:native_key.bytes
                                             length:native_key.length
                                           encoding:NSUTF8StringEncoding];
    NSString *value = [keyValueStore readValueForKey:key];
    return value != nil ? toManagedNativeString(value) : envoy_nodata;
  }
}

void ios_kv_store_save(envoy_data native_key, envoy_data native_value, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    id<EnvoyKeyValueStore> keyValueStore = (__bridge id<EnvoyKeyValueStore>)context;
    NSString *key = [[NSString alloc] initWithBytes:native_key.bytes
                                             length:native_key.length
                                           encoding:NSUTF8StringEncoding];
    NSString *value = [[NSString alloc] initWithBytes:native_value.bytes
                                               length:native_value.length
                                             encoding:NSUTF8StringEncoding];
    [keyValueStore saveValue:value toKey:key];
  }
}

void ios_kv_store_remove(envoy_data native_key, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    id<EnvoyKeyValueStore> keyValueStore = (__bridge id<EnvoyKeyValueStore>)context;
    NSString *key = [[NSString alloc] initWithBytes:native_key.bytes
                                             length:native_key.length
                                           encoding:NSUTF8StringEncoding];
    [keyValueStore removeKey:key];
  }
}
