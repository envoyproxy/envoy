#import "library/objective-c/EnvoyEngine.h"
#import "library/objective-c/EnvoyBridgeUtility.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

#import <stdatomic.h>

#pragma mark - Utility types

typedef struct {
  // The stream is kept in memory through a strong reference to itself. In order to free the
  // stream when it finishes, it is stored in this context so that it can be called when it
  // is safe to be cleaned up.
  // This approach allows `EnvoyHTTPCallbacks` to be agnostic of associated streams, enabling
  // instances to be reused with multiple streams if desired.
  __unsafe_unretained EnvoyHTTPStreamImpl *stream;
  __unsafe_unretained EnvoyHTTPCallbacks *callbacks;
  atomic_bool *closed;
} ios_context;

#pragma mark - C callbacks

static void *ios_on_headers(envoy_headers headers, bool end_stream, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onHeaders) {
      callbacks.onHeaders(to_ios_headers(headers), end_stream);
    }
  });
  return NULL;
}

static void *ios_on_data(envoy_data data, bool end_stream, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onData) {
      callbacks.onData(to_ios_data(data), end_stream);
    }
  });
  return NULL;
}

static void *ios_on_metadata(envoy_headers metadata, void *context) { return NULL; }

static void *ios_on_trailers(envoy_headers trailers, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onTrailers) {
      callbacks.onTrailers(to_ios_headers(trailers));
    }
  });
  return NULL;
}

static void *ios_on_complete(void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    // TODO: If the callback queue is not serial, clean up is not currently thread-safe.
    assert(stream);
    [stream cleanUp];
  });
  return NULL;
}

static void *ios_on_cancel(void *context) {
  // This call is atomically gated at the call-site and will only happen once. It may still fire
  // after a complete response or error callback, but no other callbacks for the stream will ever
  // fire AFTER the cancellation callback.
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onCancel) {
      callbacks.onCancel();
    }

    // TODO: If the callback queue is not serial, clean up is not currently thread-safe.
    assert(stream);
    [stream cleanUp];
  });
  return NULL;
}

static void *ios_on_error(envoy_error error, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onError) {
      NSString *errorMessage = [[NSString alloc] initWithBytes:error.message.bytes
                                                        length:error.message.length
                                                      encoding:NSUTF8StringEncoding];
      release_envoy_error(error);
      callbacks.onError(error.error_code, errorMessage, error.attempt_count);
    }

    // TODO: If the callback queue is not serial, clean up is not currently thread-safe.
    assert(stream);
    [stream cleanUp];
  });
  return NULL;
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
           explicitFlowControl:(BOOL)explicitFlowControl {
  self = [super init];
  if (!self) {
    return nil;
  }

  // Retain platform callbacks
  _platformCallbacks = callbacks;
  _streamHandle = handle;

  // Create callback context
  ios_context *context = safe_malloc(sizeof(ios_context));
  context->callbacks = callbacks;
  context->stream = self;
  context->closed = safe_malloc(sizeof(atomic_bool));
  atomic_store(context->closed, NO);

  // Create native callbacks
  envoy_http_callbacks native_callbacks = {ios_on_headers,  ios_on_data,  ios_on_metadata,
                                           ios_on_trailers, ios_on_error, ios_on_complete,
                                           ios_on_cancel,   context};
  _nativeCallbacks = native_callbacks;

  // We need create the native-held strong ref on this stream before we call start_stream because
  // start_stream could result in a reset that would release the native ref.
  _strongSelf = self;
  envoy_status_t result = start_stream(_streamHandle, native_callbacks, explicitFlowControl);
  if (result != ENVOY_SUCCESS) {
    _strongSelf = nil;
    return nil;
  }

  return self;
}

- (void)dealloc {
  ios_context *context = _nativeCallbacks.context;
  free(context->closed);
  free(context);
}

- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close {
  send_headers(_streamHandle, toNativeHeaders(headers), close);
}

- (void)sendData:(NSData *)data close:(BOOL)close {
  send_data(_streamHandle, toNativeData(data), close);
}

- (void)readData:(size_t)byteCount {
  read_data(_streamHandle, byteCount);
}

- (void)sendTrailers:(EnvoyHeaders *)trailers {
  send_trailers(_streamHandle, toNativeHeaders(trailers));
}

- (int)cancel {
  return reset_stream(_streamHandle);
}

- (void)cleanUp {
  _strongSelf = nil;
}

@end
