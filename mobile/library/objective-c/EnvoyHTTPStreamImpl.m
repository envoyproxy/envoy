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

static void *ios_on_headers(envoy_headers headers, bool end_stream, envoy_stream_intel stream_intel,
                            void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onHeaders) {
      callbacks.onHeaders(to_ios_headers(headers), end_stream, stream_intel);
    }
  });
  return NULL;
}

static void *ios_on_data(envoy_data data, bool end_stream, envoy_stream_intel stream_intel,
                         void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onData) {
      callbacks.onData(to_ios_data(data), end_stream, stream_intel);
    }
  });
  return NULL;
}

static void *ios_on_metadata(envoy_headers metadata, envoy_stream_intel stream_intel,
                             void *context) {
  return NULL;
}

static void *ios_on_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                             void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onTrailers) {
      callbacks.onTrailers(to_ios_headers(trailers), stream_intel);
    }
  });
  return NULL;
}

static void *ios_on_send_window_available(envoy_stream_intel stream_intel, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onSendWindowAvailable) {
      callbacks.onSendWindowAvailable(stream_intel);
    }
  });
  return NULL;
}

static void *ios_on_complete(envoy_stream_intel stream_intel,
                             envoy_final_stream_intel final_stream_intel, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onComplete) {
      callbacks.onComplete(stream_intel, final_stream_intel);
    }

    assert(stream);
    [stream cleanUp];
  });
  return NULL;
}

static void *ios_on_cancel(envoy_stream_intel stream_intel,
                           envoy_final_stream_intel final_stream_intel, void *context) {
  // This call is atomically gated at the call-site and will only happen once. It may still fire
  // after a complete response or error callback, but no other callbacks for the stream will ever
  // fire AFTER the cancellation callback.
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onCancel) {
      callbacks.onCancel(stream_intel, final_stream_intel);
    }

    // TODO: If the callback queue is not serial, clean up is not currently thread-safe.
    assert(stream);
    [stream cleanUp];
  });
  return NULL;
}

static void *ios_on_error(envoy_error error, envoy_stream_intel stream_intel,
                          envoy_final_stream_intel final_stream_intel, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  EnvoyHTTPStreamImpl *stream = c->stream;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onError) {
      NSString *errorMessage = [[NSString alloc] initWithBytes:error.message.bytes
                                                        length:error.message.length
                                                      encoding:NSUTF8StringEncoding];
      release_envoy_error(error);
      callbacks.onError(error.error_code, errorMessage, error.attempt_count, stream_intel,
                        final_stream_intel);
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
  envoy_engine_t _engineHandle;
}

- (instancetype)initWithHandle:(envoy_stream_t)handle
                        engine:(envoy_engine_t)engineHandle
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
  ios_context *context = (ios_context *)safe_malloc(sizeof(ios_context));
  context->callbacks = callbacks;
  context->stream = self;
  context->closed = (atomic_bool *)safe_malloc(sizeof(atomic_bool));
  atomic_store(context->closed, NO);

  // Create native callbacks
  envoy_http_callbacks native_callbacks = {
      ios_on_headers, ios_on_data,     ios_on_metadata, ios_on_trailers,
      ios_on_error,   ios_on_complete, ios_on_cancel,   ios_on_send_window_available,
      context};
  _nativeCallbacks = native_callbacks;

  _engineHandle = engineHandle;

  // We need create the native-held strong ref on this stream before we call start_stream because
  // start_stream could result in a reset that would release the native ref.
  _strongSelf = self;
  envoy_status_t result =
      start_stream(engineHandle, _streamHandle, native_callbacks, explicitFlowControl);
  if (result != ENVOY_SUCCESS) {
    _strongSelf = nil;
    return nil;
  }

  return self;
}

- (void)dealloc {
  ios_context *context = (ios_context *)_nativeCallbacks.context;
  free(context->closed);
  free(context);
}

- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close {
  send_headers(_engineHandle, _streamHandle, toNativeHeaders(headers), close);
}

- (void)sendData:(NSData *)data close:(BOOL)close {
  send_data(_engineHandle, _streamHandle, toNativeData(data), close);
}

- (void)readData:(size_t)byteCount {
  read_data(_engineHandle, _streamHandle, byteCount);
}

- (void)sendTrailers:(EnvoyHeaders *)trailers {
  send_trailers(_engineHandle, _streamHandle, toNativeHeaders(trailers));
}

- (int)cancel {
  return reset_stream(_engineHandle, _streamHandle);
}

- (void)cleanUp {
  _strongSelf = nil;
}

@end
