#include "library/common/http/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "library/common/buffer/bridge_fragment.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Http {

Dispatcher::DirectStreamCallbacks::DirectStreamCallbacks(envoy_stream_t stream,
                                                         envoy_http_callbacks bridge_callbacks,
                                                         Dispatcher& http_dispatcher)
    : stream_handle_(stream), bridge_callbacks_(bridge_callbacks),
      http_dispatcher_(http_dispatcher) {}

void Dispatcher::DirectStreamCallbacks::onHeaders(HeaderMapPtr&& headers, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response headers for stream (end_stream={}):\n{}", stream_handle_,
            end_stream, *headers);
  // TODO: ***HACK*** currently Envoy sends local replies in cases where an error ought to be
  // surfaced via the error path. There are ways we can clean up Envoy's local reply path to
  // make this possible, but nothing expedient. For the immediate term this is our only real
  // option. See https://github.com/lyft/envoy-mobile/issues/460

  // The presence of EnvoyUpstreamServiceTime implies these headers are not due to a local reply.
  if (headers->get(Headers::get().EnvoyUpstreamServiceTime) != nullptr) {
    envoy_headers bridge_headers = Utility::toBridgeHeaders(*headers);
    bridge_callbacks_.on_headers(bridge_headers, end_stream, bridge_callbacks_.context);
    return;
  }

  // We assume that local replies represent error conditions, having audited occurrences in
  // Envoy today. This is not a good long-term solution.
  uint64_t response_status = Http::Utility::getResponseStatus(*headers);
  switch (response_status) {
  case 200: {
    // We still treat successful local responses as actual success.
    envoy_headers bridge_headers = Utility::toBridgeHeaders(*headers);
    bridge_callbacks_.on_headers(bridge_headers, end_stream, bridge_callbacks_.context);
    return;
  }
  case 503:
    error_code_ = ENVOY_CONNECTION_FAILURE;
    break;
  default:
    error_code_ = ENVOY_UNDEFINED_ERROR;
  }
  ENVOY_LOG(debug, "[S{}] intercepted local response", stream_handle_);
  if (end_stream) {
    // The local stream may or may not have completed. We don't want to be tracking/synchronized
    // on that state, so we just reset everything now to ensure teardown.
    auto stream = http_dispatcher_.getStream(stream_handle_);
    ASSERT(stream);
    stream->underlying_stream_.reset();
  }
}

void Dispatcher::DirectStreamCallbacks::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response data for stream (length={} end_stream={})", stream_handle_,
            data.length(), end_stream);
  if (!error_code_.has_value()) {
    bridge_callbacks_.on_data(Buffer::Utility::toBridgeData(data), end_stream,
                              bridge_callbacks_.context);
  } else {
    ASSERT(end_stream);
    error_message_ = Buffer::Utility::toBridgeData(data);
    // The local stream may or may not have completed. We don't want to be tracking/synchronized on
    // that state, so we just reset everything now to ensure teardown.
    auto stream = http_dispatcher_.getStream(stream_handle_);
    ASSERT(stream);
    stream->underlying_stream_.reset();
  }
}

void Dispatcher::DirectStreamCallbacks::onTrailers(HeaderMapPtr&& trailers) {
  ENVOY_LOG(debug, "[S{}] response trailers for stream:\n{}", stream_handle_, *trailers);
  bridge_callbacks_.on_trailers(Utility::toBridgeHeaders(*trailers), bridge_callbacks_.context);
}

void Dispatcher::DirectStreamCallbacks::onComplete() {
  ENVOY_LOG(debug, "[S{}] complete stream", stream_handle_);
  bridge_callbacks_.on_complete(bridge_callbacks_.context);
  // Very important: onComplete and onReset both clean up stream state in the http dispatcher
  // because the underlying async client implementation **guarantees** that only onComplete **or**
  // onReset will be fired for a stream. This means it is safe to clean up the stream when either of
  // the terminal callbacks fire without keeping additional state in this layer.
  http_dispatcher_.cleanup(stream_handle_);
}

void Dispatcher::DirectStreamCallbacks::onReset() {
  ENVOY_LOG(debug, "[S{}] remote reset stream", stream_handle_);
  envoy_error_code_t code = error_code_.value_or(ENVOY_STREAM_RESET);
  envoy_data message = error_message_.value_or(envoy_nodata);
  bridge_callbacks_.on_error({code, message}, bridge_callbacks_.context);
  // Very important: onComplete and onReset both clean up stream state in the http dispatcher
  // because the underlying async client implementation **guarantees** that only onComplete **or**
  // onReset will be fired for a stream. This means it is safe to clean up the stream when either of
  // the terminal callbacks fire without keeping additional state in this layer.
  http_dispatcher_.cleanup(stream_handle_);
}

Dispatcher::DirectStream::DirectStream(envoy_stream_t stream_handle,
                                       AsyncClient::Stream& underlying_stream,
                                       DirectStreamCallbacksPtr&& callbacks)
    : stream_handle_(stream_handle), underlying_stream_(underlying_stream),
      callbacks_(std::move(callbacks)) {}

AsyncClient::StreamOptions
Dispatcher::DirectStream::toNativeStreamOptions(envoy_stream_options stream_options) {
  AsyncClient::StreamOptions native_stream_options;
  native_stream_options.setBufferBodyForRetry(stream_options.buffer_body_for_retry);
  return native_stream_options;
}

void Dispatcher::ready(Event::Dispatcher& event_dispatcher,
                       Upstream::ClusterManager& cluster_manager) {
  Thread::LockGuard lock(dispatch_lock_);

  // Drain the init_queue_ into the event_dispatcher_.
  for (const Event::PostCb& cb : init_queue_) {
    event_dispatcher.post(cb);
  }

  // Ordering somewhat matters here if concurrency guarantees are loosened (e.g. if
  // we rely on atomics instead of locks).
  event_dispatcher_ = &event_dispatcher;
  cluster_manager_ = &cluster_manager;
}

void Dispatcher::post(Event::PostCb callback) {
  Thread::LockGuard lock(dispatch_lock_);

  // If the event_dispatcher_ is set, then post the functor directly to it.
  if (event_dispatcher_ != nullptr) {
    event_dispatcher_->post(callback);
    return;
  }

  // Otherwise, push the functor to the init_queue_ which will be drained once the
  // event_dispatcher_ is ready.
  init_queue_.push_back(callback);
}

Dispatcher::Dispatcher(std::atomic<envoy_network_t>& preferred_network)
    : preferred_network_(preferred_network) {}

envoy_status_t Dispatcher::startStream(envoy_stream_t new_stream_handle,
                                       envoy_http_callbacks bridge_callbacks,
                                       envoy_stream_options stream_options) {
  post([this, new_stream_handle, bridge_callbacks, stream_options]() -> void {
    DirectStreamCallbacksPtr callbacks =
        std::make_unique<DirectStreamCallbacks>(new_stream_handle, bridge_callbacks, *this);

    AsyncClient& async_client = getClient();
    // While this struct is passed by reference to AsyncClient::start, it does not need to be
    // preserved outside of this stack frame because its values are not used beyond the return of
    // AsyncClient::start. If this changes, we need to store this struct in the DirectStream.
    AsyncClient::StreamOptions native_stream_options =
        Dispatcher::DirectStream::toNativeStreamOptions(stream_options);
    AsyncClient::Stream* underlying_stream = async_client.start(*callbacks, native_stream_options);

    if (!underlying_stream) {
      // TODO: this callback might fire before the startStream function returns.
      // Take this into account when thinking about stream cancellation.
      callbacks->onReset();
    } else {
      DirectStreamPtr direct_stream = std::make_unique<DirectStream>(
          new_stream_handle, *underlying_stream, std::move(callbacks));
      streams_.emplace(new_stream_handle, std::move(direct_stream));
      ENVOY_LOG(debug, "[S{}] start stream", new_stream_handle);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendHeaders(envoy_stream_t stream, envoy_headers headers,
                                       bool end_stream) {
  post([this, stream, headers, end_stream]() -> void {
    DirectStream* direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream != nullptr) {
      direct_stream->headers_ = Utility::toInternalHeaders(headers);
      ENVOY_LOG(debug, "[S{}] request headers for stream (end_stream={}):\n{}", stream, end_stream,
                *direct_stream->headers_);
      direct_stream->underlying_stream_.sendHeaders(*direct_stream->headers_, end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendData(envoy_stream_t stream, envoy_data data, bool end_stream) {
  post([this, stream, data, end_stream]() -> void {
    DirectStream* direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream != nullptr) {
      // The buffer is moved internally, in a synchronous fashion, so we don't need the lifetime of
      // the InstancePtr to outlive this function call.
      Buffer::InstancePtr buf = Buffer::Utility::toInternalData(data);

      ENVOY_LOG(debug, "[S{}] request data for stream (length={} end_stream={})\n", stream,
                data.length, end_stream);
      direct_stream->underlying_stream_.sendData(*buf, end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

// TODO: implement.
envoy_status_t Dispatcher::sendMetadata(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }

envoy_status_t Dispatcher::sendTrailers(envoy_stream_t stream, envoy_headers trailers) {
  post([this, stream, trailers]() -> void {
    DirectStream* direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream != nullptr) {
      direct_stream->trailers_ = Utility::toInternalHeaders(trailers);
      ENVOY_LOG(debug, "[S{}] request trailers for stream:\n{}", stream, *direct_stream->trailers_);
      direct_stream->underlying_stream_.sendTrailers(*direct_stream->trailers_);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::resetStream(envoy_stream_t stream) {
  post([this, stream]() -> void {
    DirectStream* direct_stream = getStream(stream);
    if (direct_stream) {
      direct_stream->underlying_stream_.reset();
    }
  });
  return ENVOY_SUCCESS;
}

// Select the client based on the current preferred network. This helps to ensure that
// the engine uses connections opened on the current favored interface.
AsyncClient& Dispatcher::getClient() {
  // This function must be called from the dispatcher's own thread and so this state
  // is safe to access without holding the dispatch_lock_.
  ASSERT(TS_UNCHECKED_READ(event_dispatcher_)->isThreadSafe(),
         "cluster interaction must be performed on the event_dispatcher_'s thread.");
  switch (preferred_network_.load()) {
  case ENVOY_NET_WLAN:
    // The ASSERT above ensures the cluster_manager_ is safe to access.
    return TS_UNCHECKED_READ(cluster_manager_)->httpAsyncClientForCluster("base_wlan");
  case ENVOY_NET_WWAN:
    return TS_UNCHECKED_READ(cluster_manager_)->httpAsyncClientForCluster("base_wwan");
  case ENVOY_NET_GENERIC:
  default:
    return TS_UNCHECKED_READ(cluster_manager_)->httpAsyncClientForCluster("base");
  }
}

Dispatcher::DirectStream* Dispatcher::getStream(envoy_stream_t stream) {
  // The dispatch_lock_ does not need to guard the event_dispatcher_ pointer here because this
  // function should only be called from the context of Envoy's event dispatcher.
  ASSERT(TS_UNCHECKED_READ(event_dispatcher_)->isThreadSafe(),
         "stream interaction must be performed on the event_dispatcher_'s thread.");
  auto direct_stream_pair_it = streams_.find(stream);
  return (direct_stream_pair_it != streams_.end()) ? direct_stream_pair_it->second.get() : nullptr;
}

void Dispatcher::cleanup(envoy_stream_t stream_handle) {
  DirectStream* direct_stream = getStream(stream_handle);

  RELEASE_ASSERT(direct_stream,
                 "cleanup is a private method that is only called with stream ids that exist");

  size_t erased = streams_.erase(stream_handle);
  ASSERT(erased == 1, "cleanup should always remove one entry from the streams map");
  ENVOY_LOG(debug, "[S{}] remove stream", stream_handle);
}

} // namespace Http
} // namespace Envoy
