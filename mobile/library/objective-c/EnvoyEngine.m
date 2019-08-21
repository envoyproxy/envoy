#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"
#import "library/common/include/c_types.h"

#import <stdatomic.h>

#pragma mark - EnvoyObserver

@implementation EnvoyObserver
@end

#pragma mark - EnvoyEngineImpl

@implementation EnvoyEngineImpl {
  envoy_engine_t _engineHandle;
}

- (instancetype)init {
  self = [super init];
  if (!self) {
    return nil;
  }
  _engineHandle = init_engine();
  return self;
}

- (int)runWithConfig:(NSString *)config {
  return [self runWithConfig:config logLevel:@"info"];
}

- (int)runWithConfig:(NSString *)config logLevel:(NSString *)logLevel {
  // Envoy exceptions will only be caught here when compiled for 64-bit arches.
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Exceptions/Articles/Exceptions64Bit.html
  @try {
    return (int)run_engine(config.UTF8String, logLevel.UTF8String);
  } @catch (...) {
    NSLog(@"Envoy exception caught.");
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyException" object:self];
    return 1;
  }
}

- (EnvoyHTTPStream *)startStreamWithObserver:(EnvoyObserver *)observer {
  return [[EnvoyHTTPStream alloc] initWithHandle:init_stream(_engineHandle) observer:observer];
}

@end

#pragma mark - EnvoyConfiguration

@implementation EnvoyConfiguration

+ (NSString *)templateString {
  return [[NSString alloc] initWithUTF8String:config_template];
}

@end

#pragma mark - Utilities to move elsewhere

typedef struct {
  EnvoyObserver *observer;
  atomic_bool *canceled;
} ios_context;

// static envoy_data toUnmanagedNativeData(NSData *data) {
//   // TODO: implement me
//   return envoy_nodata;
// }

static void ios_free_native_data(void *context) { free(context); }

// static void ios_release_native_data(void *context) {
//   // TODO: implement me
// }

static envoy_data toManagedNativeString(NSString *s) {
  size_t length = s.length;
  uint8_t *native_string = (uint8_t *)malloc(sizeof(uint8_t) * length);
  memcpy(native_string, s.UTF8String, length);
  envoy_data ret = {length, native_string, ios_free_native_data, native_string};
  return ret;
}

static envoy_headers toNativeHeaders(EnvoyHeaders *headers) {
  envoy_header_size_t length = 0;
  for (id headerKey in headers) {
    length += [headers[headerKey] count];
  }
  envoy_header *header_array = (envoy_header *)malloc(sizeof(envoy_header) * length);
  envoy_header_size_t header_index = 0;
  for (id headerKey in headers) {
    NSArray *headerList = headers[headerKey];
    for (id headerValue in headerList) {
      envoy_header new_header = {toManagedNativeString(headerKey),
                                 toManagedNativeString(headerValue)};
      header_array[header_index++] = new_header;
    }
  }
  // ASSERT(header_index == length);
  envoy_headers ret = {length, header_array};
  return ret;
}

static NSData *to_ios_data(envoy_data data) {
  // TODO: investigate buffer ownership
  // Possibly extend/subclass NSData to call envoy_data.release on dealloc and have release drain
  // the underlying Envoy buffer instance
  return [NSData dataWithBytes:(void *)data.bytes length:data.length];
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
  EnvoyObserver *observer = c->observer;
  // TODO: protection against null pointers.
  dispatch_async(observer.dispatchQueue, ^{
    if (atomic_load(c->canceled)) {
      return;
    }
    observer.onHeaders(to_ios_headers(headers), end_stream);
  });
}

static void ios_on_data(envoy_data data, bool end_stream, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  dispatch_async(observer.dispatchQueue, ^{
    if (atomic_load(c->canceled)) {
      return;
    }
    // TODO: retain data
    observer.onData(to_ios_data(data), end_stream);
  });
}

static void ios_on_metadata(envoy_headers metadata, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  dispatch_async(observer.dispatchQueue, ^{
    if (atomic_load(c->canceled)) {
      return;
    }
    observer.onMetadata(to_ios_headers(metadata));
  });
}

static void ios_on_trailers(envoy_headers trailers, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  dispatch_async(observer.dispatchQueue, ^{
    if (atomic_load(c->canceled)) {
      return;
    }
    observer.onTrailers(to_ios_headers(trailers));
  });
}

static void ios_on_complete(void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  dispatch_async(observer.dispatchQueue, ^{
    // TODO: release stream
    if (atomic_load(c->canceled)) {
      return;
    }
  });
}

static void ios_on_cancel(void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  // TODO: release stream
  dispatch_async(observer.dispatchQueue, ^{
    // This call is atomically gated at the call-site and will only happen once.
    observer.onCancel();
  });
}

static void ios_on_error(envoy_error error, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyObserver *observer = c->observer;
  dispatch_async(observer.dispatchQueue, ^{
    // TODO: release stream
    if (atomic_load(c->canceled)) {
      return;
    }
    // FIXME transform error and pass up
    observer.onError();
  });
}

#pragma mark - EnvoyHTTPStream

@implementation EnvoyHTTPStream {
  EnvoyHTTPStream *_strongSelf;
  EnvoyObserver *_platformObserver;
  envoy_observer _nativeObserver;
  envoy_stream_t _streamHandle;
}

- (instancetype)initWithHandle:(uint64_t)handle observer:(EnvoyObserver *)observer {
  self = [super init];
  if (!self) {
    return nil;
  }

  _streamHandle = handle;
  // Retain platform observer
  _platformObserver = observer;

  // Create callback context
  ios_context *context = malloc(sizeof(ios_context));
  context->observer = observer;
  context->canceled = malloc(sizeof(atomic_bool));
  atomic_store(context->canceled, NO);

  // Create native observer
  envoy_observer native_obs = {ios_on_headers, ios_on_data,     ios_on_trailers, ios_on_metadata,
                               ios_on_error,   ios_on_complete, context};
  _nativeObserver = native_obs;

  envoy_status_t result = start_stream(_streamHandle, native_obs);
  if (result != ENVOY_SUCCESS) {
    return nil;
  }

  _strongSelf = self;
  return self;
}

- (void)dealloc {
  envoy_observer native_obs = _nativeObserver;
  ios_context *context = native_obs.context;
  free(context->canceled);
  free(context);
}

- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close {
  send_headers(_streamHandle, toNativeHeaders(headers), close);
}

- (void)sendData:(NSData *)data close:(BOOL)close {
  // TODO: implement
  // send_data(_streamHandle, toNativeData(data), close);
}

- (void)sendMetadata:(EnvoyHeaders *)metadata {
  send_metadata(_streamHandle, toNativeHeaders(metadata));
}

- (void)sendTrailers:(EnvoyHeaders *)trailers {
  send_trailers(_streamHandle, toNativeHeaders(trailers));
}

- (int)cancel {
  ios_context *context = _nativeObserver.context;
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
