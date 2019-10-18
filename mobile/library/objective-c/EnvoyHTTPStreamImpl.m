#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

#import <stdatomic.h>

#pragma mark - Utility types and functions

typedef struct {
  EnvoyHTTPCallbacks *callbacks;
  atomic_bool *canceled;
} ios_context;

static envoy_data toNativeData(NSData *data) {
  uint8_t *native_bytes = (uint8_t *)safe_malloc(sizeof(uint8_t) * data.length);
  memcpy(native_bytes, data.bytes, data.length);
  envoy_data ret = {data.length, native_bytes, free, native_bytes};
  return ret;
}

static envoy_data toManagedNativeString(NSString *s) {
  size_t length = s.length;
  uint8_t *native_string = (uint8_t *)safe_malloc(sizeof(uint8_t) * length);
  memcpy(native_string, s.UTF8String, length);
  envoy_data ret = {length, native_string, free, native_string};
  return ret;
}

static envoy_headers toNativeHeaders(EnvoyHeaders *headers) {
  envoy_header_size_t length = 0;
  for (id headerKey in headers) {
    length += [headers[headerKey] count];
  }
  envoy_header *header_array = (envoy_header *)safe_malloc(sizeof(envoy_header) * length);
  envoy_header_size_t header_index = 0;
  for (id headerKey in headers) {
    NSArray *headerList = headers[headerKey];
    for (id headerValue in headerList) {
      envoy_header new_header = {toManagedNativeString(headerKey),
                                 toManagedNativeString(headerValue)};
      header_array[header_index++] = new_header;
    }
  }
  // TODO: ASSERT(header_index == length);
  envoy_headers ret = {length, header_array};
  return ret;
}

static NSData *to_ios_data(envoy_data data) {
  // TODO: we are copying from envoy_data to NSData.
  NSData *platformData = [NSData dataWithBytes:(void *)data.bytes length:data.length];
  data.release(data.context);
  return platformData;
}

static EnvoyHeaders *to_ios_headers(envoy_headers headers) {
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

#pragma mark - C callbacks

static void ios_on_headers(envoy_headers headers, bool end_stream, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (atomic_load(c->canceled) || !callbacks.onHeaders) {
      return;
    }
    callbacks.onHeaders(to_ios_headers(headers), end_stream);
  });
}

static void ios_on_data(envoy_data data, bool end_stream, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (atomic_load(c->canceled) || !callbacks.onData) {
      return;
    }
    callbacks.onData(to_ios_data(data), end_stream);
  });
}

static void ios_on_metadata(envoy_headers metadata, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (atomic_load(c->canceled) || !callbacks.onMetadata) {
      return;
    }
    callbacks.onMetadata(to_ios_headers(metadata));
  });
}

static void ios_on_trailers(envoy_headers trailers, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (atomic_load(c->canceled) || !callbacks.onTrailers) {
      return;
    }
    callbacks.onTrailers(to_ios_headers(trailers));
  });
}

static void ios_on_complete(void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    // TODO: release stream
    if (atomic_load(c->canceled)) {
      return;
    }
  });
}

static void ios_on_cancel(void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  // TODO: release stream
  dispatch_async(callbacks.dispatchQueue, ^{
    // This call is atomically gated at the call-site and will only happen once.
    if (!callbacks.onCancel) {
      return;
    }
    callbacks.onCancel();
  });
}

static void ios_on_error(envoy_error error, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    // TODO: release stream
    if (atomic_load(c->canceled) || !callbacks.onError) {
      return;
    }
    NSString *errorMessage = [[NSString alloc] initWithBytes:error.message.bytes
                                                      length:error.message.length
                                                    encoding:NSUTF8StringEncoding];
    error.message.release(error.message.context);
    callbacks.onError(error.error_code, errorMessage);
  });
}

#pragma mark - EnvoyHTTPStreamImpl

@implementation EnvoyHTTPStreamImpl {
  EnvoyHTTPStreamImpl *_strongSelf;
  EnvoyHTTPCallbacks *_platformCallbacks;
  envoy_http_callbacks _nativeCallbacks;
  envoy_stream_t _streamHandle;
}

- (instancetype)initWithHandle:(envoy_stream_t)handle
                     callbacks:(EnvoyHTTPCallbacks *)callbacks
                bufferForRetry:(BOOL)bufferForRetry {
  self = [super init];
  if (!self) {
    return nil;
  }

  _streamHandle = handle;
  // Retain platform callbacks
  _platformCallbacks = callbacks;

  // Create callback context
  ios_context *context = safe_malloc(sizeof(ios_context));
  context->callbacks = callbacks;
  context->canceled = safe_malloc(sizeof(atomic_bool));
  atomic_store(context->canceled, NO);

  // Create native callbacks
  envoy_http_callbacks native_callbacks = {ios_on_headers,  ios_on_data,  ios_on_trailers,
                                           ios_on_metadata, ios_on_error, ios_on_complete,
                                           context};
  _nativeCallbacks = native_callbacks;

  // We need create the native-held strong ref on this stream before we call start_stream because
  // start_stream could result in a reset that would release the native ref.
  // TODO: To be truly safe we probably need stronger guarantees of operation ordering on this ref
  _strongSelf = self;
  envoy_stream_options stream_options = {bufferForRetry};
  envoy_status_t result = start_stream(_streamHandle, native_callbacks, stream_options);
  if (result != ENVOY_SUCCESS) {
    _strongSelf = nil;
    return nil;
  }

  return self;
}

- (void)dealloc {
  envoy_http_callbacks native_callbacks = _nativeCallbacks;
  ios_context *context = native_callbacks.context;
  free(context->canceled);
  free(context);
}

- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close {
  send_headers(_streamHandle, toNativeHeaders(headers), close);
}

- (void)sendData:(NSData *)data close:(BOOL)close {
  send_data(_streamHandle, toNativeData(data), close);
}

- (void)sendMetadata:(EnvoyHeaders *)metadata {
  send_metadata(_streamHandle, toNativeHeaders(metadata));
}

- (void)sendTrailers:(EnvoyHeaders *)trailers {
  send_trailers(_streamHandle, toNativeHeaders(trailers));
}

- (int)cancel {
  ios_context *context = _nativeCallbacks.context;
  // Step 1: atomically and synchronously prevent the execution of further callbacks other than
  // on_cancel.
  if (!atomic_exchange(context->canceled, YES)) {
    // Step 2: directly fire the cancel callback.
    ios_on_cancel(context);
    // Step 3: propagate the reset into native code.
    reset_stream(_streamHandle);
    return 0;
  } else {
    return 1;
  }
}

@end
