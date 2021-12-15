#import <Foundation/Foundation.h>

#import "library/common/types/c_types.h"

static inline envoy_data toNativeData(NSData *data) {
  if (data == nil || [data isEqual:[NSNull null]]) {
    return envoy_nodata;
  }

  uint8_t *native_bytes = (uint8_t *)safe_malloc(sizeof(uint8_t) * data.length);
  memcpy(native_bytes, data.bytes, data.length); // NOLINT(safe-memcpy)
  envoy_data ret = {data.length, native_bytes, free, native_bytes};
  return ret;
}

static inline envoy_data *toNativeDataPtr(NSData *data) {
  if (data == nil || [data isEqual:[NSNull null]]) {
    return NULL;
  }

  envoy_data *ret = (envoy_data *)safe_malloc(sizeof(envoy_data));
  *ret = toNativeData(data);
  return ret;
}

static inline envoy_data toManagedNativeString(NSString *s) {
  size_t length = [s lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
  uint8_t *native_string = (uint8_t *)safe_malloc(sizeof(uint8_t) * length);
  memcpy(native_string, s.UTF8String, length); // NOLINT(safe-memcpy)
  envoy_data ret = {length, native_string, free, native_string};
  return ret;
}

static inline envoy_headers toNativeHeaders(EnvoyHeaders *headers) {
  if (headers == nil || [headers isEqual:[NSNull null]]) {
    return envoy_noheaders;
  }

  envoy_map_size_t length = 0;
  for (NSString *headerKey in headers) {
    length += [headers[headerKey] count];
  }
  envoy_map_entry *header_array = (envoy_map_entry *)safe_malloc(sizeof(envoy_map_entry) * length);
  envoy_map_size_t header_index = 0;
  for (id headerKey in headers) {
    NSArray *headerList = headers[headerKey];
    for (NSString *headerValue in headerList) {
      envoy_map_entry new_header = {toManagedNativeString(headerKey),
                                    toManagedNativeString(headerValue)};
      header_array[header_index++] = new_header;
    }
  }
  // TODO: ASSERT(header_index == length);
  envoy_headers ret = {length, header_array};
  return ret;
}

static inline envoy_headers *toNativeHeadersPtr(EnvoyHeaders *headers) {
  if (headers == nil || [headers isEqual:[NSNull null]]) {
    return NULL;
  }

  envoy_headers *ret = (envoy_headers *)safe_malloc(sizeof(envoy_headers));
  *ret = toNativeHeaders(headers);
  return ret;
}

static inline envoy_stats_tags toNativeStatsTags(EnvoyTags *tags) {
  if (tags == nil || [tags isEqual:[NSNull null]]) {
    return envoy_stats_notags;
  }

  int length = (int)[tags count];

  envoy_map_entry *tag_array = (envoy_map_entry *)safe_malloc(sizeof(envoy_map_entry) * length);
  envoy_map_size_t tag_index = 0;
  for (id tagKey in tags) {
    NSString *tagValue = tags[tagKey];

    envoy_map_entry new_tag = {toManagedNativeString(tagKey), toManagedNativeString(tagValue)};
    tag_array[tag_index++] = new_tag;
  }
  envoy_stats_tags ret = {tag_index, tag_array};
  return ret;
}

static inline NSData *to_ios_data(envoy_data data) {
  // TODO: we are copying from envoy_data to NSData.
  // https://github.com/envoyproxy/envoy-mobile/issues/398
  NSData *platformData = [NSData dataWithBytes:(void *)data.bytes length:data.length];
  release_envoy_data(data);
  return platformData;
}

static inline NSString *to_ios_string_no_release(envoy_data data) {
  NSString *platformString = [[NSString alloc] initWithBytes:data.bytes
                                                      length:data.length
                                                    encoding:NSUTF8StringEncoding];

  if (platformString == nil) {
    NSData *bridgedData = [NSData dataWithBytes:(void *)data.bytes length:data.length];
    BOOL usedLossyConversion = NO;
    NSStringEncoding guessedEncoding = [NSString stringEncodingForData:bridgedData
                                                       encodingOptions:@{}
                                                       convertedString:&platformString
                                                   usedLossyConversion:&usedLossyConversion];
    // TODO(jpsim): Use ENVOY_LOG_EVENT to emit log events instead of NSLog.
    // We can't right now because we're not in a C++ context.
    if (platformString == nil) {
      NSLog(@"Could not convert envoy_data (%@ bytes) to NSString", @(data.length));
    } else {
      NSLog(@"envoy_data was converted to NSString using encoding %@: %@", @(guessedEncoding),
            platformString);
    }
  }

  return platformString;
}

static inline NSString *to_ios_string(envoy_data data) {
  NSString *platformString = to_ios_string_no_release(data);
  release_envoy_data(data);
  return platformString;
}

static inline EnvoyEvent *to_ios_map(envoy_map map) {
  NSMutableDictionary *newMap = [NSMutableDictionary new];
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    envoy_map_entry entry = map.entries[i];
    NSString *entryKey = to_ios_string_no_release(entry.key);
    NSString *entryValue = to_ios_string_no_release(entry.value);
    newMap[entryKey] = entryValue;
  }

  release_envoy_map(map);
  return newMap;
}

static inline EnvoyHeaders *to_ios_headers(envoy_headers headers) {
  NSMutableDictionary *headerDict = [NSMutableDictionary new];
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    envoy_map_entry header = headers.entries[i];
    NSString *headerKey = to_ios_string_no_release(header.key);
    NSString *headerValue = to_ios_string_no_release(header.value);
    // TODO: https://github.com/envoyproxy/envoy-mobile/issues/1825. All header values passed in
    // here should be valid.
    if (headerKey != nil && headerValue != nil) {
      // Ensure list is present in dictionary value
      NSMutableArray *headerValueList = headerDict[headerKey];
      if (headerValueList == nil) {
        headerValueList = [NSMutableArray new];
        headerDict[headerKey] = headerValueList;
      }
      [headerValueList addObject:headerValue];
    }
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return headerDict;
}
