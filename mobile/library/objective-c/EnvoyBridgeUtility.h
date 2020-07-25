#import <Foundation/Foundation.h>

#import "library/common/types/c_types.h"

static inline envoy_data toNativeData(NSData *data) {
  uint8_t *native_bytes = (uint8_t *)safe_malloc(sizeof(uint8_t) * data.length);
  memcpy(native_bytes, data.bytes, data.length);
  envoy_data ret = {data.length, native_bytes, free, native_bytes};
  return ret;
}

static inline envoy_data toManagedNativeString(NSString *s) {
  size_t length = s.length;
  uint8_t *native_string = (uint8_t *)safe_malloc(sizeof(uint8_t) * length);
  memcpy(native_string, s.UTF8String, length);
  envoy_data ret = {length, native_string, free, native_string};
  return ret;
}

static inline envoy_headers toNativeHeaders(EnvoyHeaders *headers) {
  envoy_header_size_t length = 0;
  for (NSString *headerKey in headers) {
    length += [headers[headerKey] count];
  }
  envoy_header *header_array = (envoy_header *)safe_malloc(sizeof(envoy_header) * length);
  envoy_header_size_t header_index = 0;
  for (id headerKey in headers) {
    NSArray *headerList = headers[headerKey];
    for (NSString *headerValue in headerList) {
      envoy_header new_header = {toManagedNativeString(headerKey),
                                 toManagedNativeString(headerValue)};
      header_array[header_index++] = new_header;
    }
  }
  // TODO: ASSERT(header_index == length);
  envoy_headers ret = {length, header_array};
  return ret;
}

static inline NSData *to_ios_data(envoy_data data) {
  // TODO: we are copying from envoy_data to NSData.
  // https://github.com/lyft/envoy-mobile/issues/398
  NSData *platformData = [NSData dataWithBytes:(void *)data.bytes length:data.length];
  data.release(data.context);
  return platformData;
}

static inline EnvoyHeaders *to_ios_headers(envoy_headers headers) {
  NSMutableDictionary *headerDict = [NSMutableDictionary new];
  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    envoy_header header = headers.headers[i];
    NSString *headerKey = [[NSString alloc] initWithBytes:header.key.bytes
                                                   length:header.key.length
                                                 encoding:NSUTF8StringEncoding];
    NSString *headerValue = [[NSString alloc] initWithBytes:header.value.bytes
                                                     length:header.value.length
                                                   encoding:NSUTF8StringEncoding];
    NSMutableArray *headerValueList = headerDict[headerKey];
    if (headerValueList == nil) {
      headerValueList = [NSMutableArray new];
      headerDict[headerKey] = headerValueList;
    }
    [headerValueList addObject:headerValue];
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return headerDict;
}
