#import "library/objective-c/EnvoyEngine.h"
#import "library/objective-c/EnvoyBridgeUtility.h"

#include "source/common/buffer/buffer_impl.h"

#import "library/common/types/c_types.h"
#import "library/common/internal_engine.h"
#include "library/common/http/header_utility.h"

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

static void ios_on_headers(envoy_headers headers, bool end_stream, envoy_stream_intel stream_intel,
                           void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onHeaders) {
      callbacks.onHeaders(to_ios_headers(headers), end_stream, stream_intel);
    }
  });
}

static void ios_on_data(envoy_data data, bool end_stream, envoy_stream_intel stream_intel,
                        void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onData) {
      callbacks.onData(to_ios_data(data), end_stream, stream_intel);
    }
  });
}

static void ios_on_metadata(envoy_headers metadata, envoy_stream_intel stream_intel,
                            void *context) {}

static void ios_on_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                            void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onTrailers) {
      callbacks.onTrailers(to_ios_headers(trailers), stream_intel);
    }
  });
}

static void ios_on_send_window_available(envoy_stream_intel stream_intel, void *context) {
  ios_context *c = (ios_context *)context;
  EnvoyHTTPCallbacks *callbacks = c->callbacks;
  dispatch_async(callbacks.dispatchQueue, ^{
    if (callbacks.onSendWindowAvailable) {
      callbacks.onSendWindowAvailable(stream_intel);
    }
  });
}

static void ios_on_complete(envoy_stream_intel stream_intel,
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
}

static void ios_on_cancel(envoy_stream_intel stream_intel,
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
}

static void ios_on_error(envoy_error error, envoy_stream_intel stream_intel,
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
}

#pragma mark - EnvoyHTTPStreamImpl

@implementation EnvoyHTTPStreamImpl {
  EnvoyHTTPStreamImpl *_strongSelf;
  EnvoyHTTPCallbacks *_platformCallbacks;
  envoy_http_callbacks _nativeCallbacks;
  envoy_stream_t _streamHandle;
  Envoy::InternalEngine *_engine;
}

- (instancetype)initWithHandle:(envoy_stream_t)handle
                        engine:(intptr_t)engine
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

  _engine = reinterpret_cast<Envoy::InternalEngine *>(engine);

  // We need create the native-held strong ref on this stream before we call start_stream because
  // start_stream could result in a reset that would release the native ref.
  _strongSelf = self;
  _streamHandle = _engine->initStream();
  _engine->startStream(_streamHandle, native_callbacks, explicitFlowControl);

  return self;
}

- (void)dealloc {
  ios_context *context = (ios_context *)_nativeCallbacks.context;
  free(context->closed);
  free(context);
}

- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close {
  Envoy::Http::RequestHeaderMapPtr cppHeaders = Envoy::Http::Utility::createRequestHeaderMapPtr();
  for (id headerKey in headers) {
    std::string cppHeaderKey = std::string([headerKey UTF8String]);
    if (cppHeaders->formatter().has_value()) {
      Envoy::Http::StatefulHeaderKeyFormatter &formatter = cppHeaders->formatter().value();
      // Make sure the formatter knows the original case.
      formatter.processKey(cppHeaderKey);
    }
    NSArray *headerList = headers[headerKey];
    for (NSString *headerValue in headerList) {
      std::string cppHeaderValue = std::string([headerValue UTF8String]);
      cppHeaders->addCopy(Envoy::Http::LowerCaseString(cppHeaderKey), cppHeaderValue);
    }
  }
  _engine->sendHeaders(_streamHandle, std::move(cppHeaders), close);
}

- (void)sendData:(NSData *)data close:(BOOL)close {
  Envoy::Buffer::InstancePtr buffer = std::make_unique<Envoy::Buffer::OwnedImpl>();
  buffer->add([data bytes], data.length);
  _engine->sendData(_streamHandle, std::move(buffer), close);
}

- (void)readData:(size_t)byteCount {
  _engine->readData(_streamHandle, byteCount);
}

- (void)sendTrailers:(EnvoyHeaders *)trailers {
  Envoy::Http::RequestTrailerMapPtr cppTrailers =
      Envoy::Http::Utility::createRequestTrailerMapPtr();
  for (id trailerKey in trailers) {
    std::string cppTrailerKey = std::string([trailerKey UTF8String]);
    NSArray *trailerList = trailers[trailerKey];
    for (NSString *trailerValue in trailerList) {
      std::string cppTrailerValue = std::string([trailerValue UTF8String]);
      cppTrailers->addCopy(Envoy::Http::LowerCaseString(cppTrailerKey), cppTrailerValue);
    }
  }
  _engine->sendTrailers(_streamHandle, std::move((cppTrailers)));
}

- (int)cancel {
  _engine->cancelStream(_streamHandle);
  return ENVOY_SUCCESS;
}

- (void)cleanUp {
  _strongSelf = nil;
}

@end
