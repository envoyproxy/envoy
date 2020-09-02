#include "library/common/http/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "library/common/buffer/bridge_fragment.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/network/synthetic_address_impl.h"
#include "library/common/thread/lock_guard.h"

namespace Envoy {
namespace Http {

/**
 * IMPORTANT: stream closure semantics in envoy mobile depends on the fact that the HCM fires a
 * stream reset when the remote side of the stream is closed but the local side remains open.
 * In other words the HCM (like the rest of Envoy) dissallows locally half-open streams.
 * If this changes in Envoy, this file will need to change as well.
 * For implementation details @see Dispatcher::DirectStreamCallbacks::closeRemote.
 */

Dispatcher::DirectStreamCallbacks::DirectStreamCallbacks(DirectStream& direct_stream,
                                                         envoy_http_callbacks bridge_callbacks,
                                                         Dispatcher& http_dispatcher)
    : direct_stream_(direct_stream), bridge_callbacks_(bridge_callbacks),
      http_dispatcher_(http_dispatcher) {}

void Dispatcher::DirectStreamCallbacks::encodeHeaders(const ResponseHeaderMap& headers,
                                                      bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);

  ASSERT(http_dispatcher_.getStream(direct_stream_.stream_handle_));

  uint64_t response_status = Utility::getResponseStatus(headers);
  // Track success for later bookkeeping (stream could still be reset).
  success_ = CodeUtility::is2xx(response_status);

  if (end_stream) {
    closeStream();
  }

  // TODO: ***HACK*** currently Envoy sends local replies in cases where an error ought to be
  // surfaced via the error path. There are ways we can clean up Envoy's local reply path to
  // make this possible, but nothing expedient. For the immediate term this is our only real
  // option. See https://github.com/lyft/envoy-mobile/issues/460

  // Error path: missing EnvoyUpstreamServiceTime implies this is a local reply, which we treat as
  // a stream error.
  if (!success_ && headers.get(Headers::get().EnvoyUpstreamServiceTime) == nullptr) {
    ENVOY_LOG(debug, "[S{}] intercepted local response", direct_stream_.stream_handle_);
    mapLocalResponseToError(headers);
    if (end_stream) {
      onReset();
    }
    return;
  }

  // Normal response path.

  // Testing hook.
  http_dispatcher_.synchronizer_.syncPoint("dispatch_encode_headers");

  ENVOY_LOG(debug, "[S{}] dispatching to platform response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);
  bridge_callbacks_.on_headers(Utility::toBridgeHeaders(headers), end_stream,
                               bridge_callbacks_.context);
  if (end_stream) {
    onComplete();
  }
}

void Dispatcher::DirectStreamCallbacks::mapLocalResponseToError(const ResponseHeaderMap& headers) {
  // Deal with a local response based on the HTTP status code received. Envoy Mobile treats
  // successful local responses as actual success. Envoy Mobile surfaces non-200 local responses as
  // errors via callbacks rather than an HTTP response. This is inline with behaviour of other
  // mobile networking libraries.
  switch (Utility::getResponseStatus(headers)) {
  case 503:
    error_code_ = ENVOY_CONNECTION_FAILURE;
    break;
  default:
    error_code_ = ENVOY_UNDEFINED_ERROR;
  }

  uint32_t attempt_count;
  if (headers.EnvoyAttemptCount() &&
      absl::SimpleAtoi(headers.EnvoyAttemptCount()->value().getStringView(), &attempt_count)) {
    error_attempt_count_ = attempt_count;
  }
}

void Dispatcher::DirectStreamCallbacks::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);

  ASSERT(http_dispatcher_.getStream(direct_stream_.stream_handle_));
  if (end_stream) {
    closeStream();
  }

  // Error path.
  if (error_code_.has_value()) {
    ASSERT(end_stream,
           "local response has to end the stream with a single data frame. If Envoy changes "
           "this expectation, this code needs to be updated.");
    error_message_ = Buffer::Utility::toBridgeData(data);
    onReset();
    return;
  }

  // Normal path.

  // Testing hook.
  if (end_stream) {
    http_dispatcher_.synchronizer_.syncPoint("dispatch_encode_final_data");
  }

  ENVOY_LOG(debug,
            "[S{}] dispatching to platform response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);
  bridge_callbacks_.on_data(Buffer::Utility::toBridgeData(data), end_stream,
                            bridge_callbacks_.context);
  if (end_stream) {
    onComplete();
  }
}

void Dispatcher::DirectStreamCallbacks::encodeTrailers(const ResponseTrailerMap& trailers) {
  ENVOY_LOG(debug, "[S{}] response trailers for stream:\n{}", direct_stream_.stream_handle_,
            trailers);

  ASSERT(http_dispatcher_.getStream(direct_stream_.stream_handle_));
  closeStream(); // Trailers always indicate the end of the stream.

  ENVOY_LOG(debug, "[S{}] dispatching to platform response trailers for stream:\n{}",
            direct_stream_.stream_handle_, trailers);
  bridge_callbacks_.on_trailers(Utility::toBridgeHeaders(trailers), bridge_callbacks_.context);
  onComplete();
}

void Dispatcher::DirectStreamCallbacks::closeStream() {
  // Envoy itself does not currently allow half-open streams where the local half is open
  // but the remote half is closed. Note that if local is open, Envoy will reset the stream.
  http_dispatcher_.removeStream(direct_stream_.stream_handle_);
}

void Dispatcher::DirectStreamCallbacks::onComplete() {
  ENVOY_LOG(debug, "[S{}] complete stream (success={})", direct_stream_.stream_handle_, success_);
  if (success_) {
    http_dispatcher_.stats().stream_success_.inc();
  } else {
    http_dispatcher_.stats().stream_failure_.inc();
  }
  bridge_callbacks_.on_complete(bridge_callbacks_.context);
}

void Dispatcher::DirectStreamCallbacks::onReset() {
  ENVOY_LOG(debug, "[S{}] remote reset stream", direct_stream_.stream_handle_);

  // The stream should no longer be preset in the map, because onReset() was either called from a
  // terminal callback that mapped to an error or it was called in response to a resetStream().
  ASSERT(!http_dispatcher_.getStream(direct_stream_.stream_handle_));
  envoy_error_code_t code = error_code_.value_or(ENVOY_STREAM_RESET);
  envoy_data message = error_message_.value_or(envoy_nodata);
  int32_t attempt_count = error_attempt_count_.value_or(-1);

  // Testing hook.
  http_dispatcher_.synchronizer_.syncPoint("dispatch_on_error");

  ENVOY_LOG(debug, "[S{}] dispatching to platform remote reset stream",
            direct_stream_.stream_handle_);
  http_dispatcher_.stats().stream_failure_.inc();
  bridge_callbacks_.on_error({code, message, attempt_count}, bridge_callbacks_.context);
}

void Dispatcher::DirectStreamCallbacks::onCancel() {
  ENVOY_LOG(debug, "[S{}] dispatching to platform cancel stream", direct_stream_.stream_handle_);
  http_dispatcher_.stats().stream_cancel_.inc();
  bridge_callbacks_.on_cancel(bridge_callbacks_.context);
}

Dispatcher::DirectStream::DirectStream(envoy_stream_t stream_handle, Dispatcher& http_dispatcher)
    : stream_handle_(stream_handle), parent_(http_dispatcher) {}

Dispatcher::DirectStream::~DirectStream() {
  ENVOY_LOG(debug, "[S{}] destroy stream", stream_handle_);
}

void Dispatcher::DirectStream::resetStream(StreamResetReason reason) {
  // This seems in line with other codec implementations, and so the assumption is that this is in
  // line with upstream expectations.
  // TODO(goaway): explore an upstream fix to get the HCM to clean up ActiveStream itself.
  runResetCallbacks(reason);
  if (!parent_.getStream(stream_handle_)) {
    // We don't assert here, because Envoy will issue a stream reset if a stream closes remotely
    // while still open locally. In this case the stream will already have been removed from
    // our streams_ map due to the remote closure.
    return;
  }
  parent_.removeStream(stream_handle_);
  callbacks_->onReset();
}

Dispatcher::Dispatcher(std::atomic<envoy_network_t>& preferred_network)
    : stats_prefix_("http.dispatcher."), preferred_network_(preferred_network),
      address_(std::make_shared<Network::Address::SyntheticAddressImpl>()) {}

void Dispatcher::ready(Event::Dispatcher& event_dispatcher, Stats::Scope& scope,
                       ApiListener& api_listener) {
  Thread::LockGuard lock(ready_lock_);

  // Drain the init_queue_ into the event_dispatcher_.
  for (const Event::PostCb& cb : init_queue_) {
    event_dispatcher.post(cb);
  }

  // Ordering somewhat matters here if concurrency guarantees are loosened (e.g. if
  // we rely on atomics instead of locks).
  stats_.emplace(generateStats(stats_prefix_, scope));
  event_dispatcher_ = &event_dispatcher;
  api_listener_ = &api_listener;
}

void Dispatcher::post(Event::PostCb callback) {
  Thread::LockGuard lock(ready_lock_);

  // If the event_dispatcher_ is set, then post the functor directly to it.
  if (event_dispatcher_ != nullptr) {
    event_dispatcher_->post(callback);
    return;
  }

  // Otherwise, push the functor to the init_queue_ which will be drained once the
  // event_dispatcher_ is ready.
  init_queue_.push_back(callback);
}

envoy_status_t Dispatcher::startStream(envoy_stream_t new_stream_handle,
                                       envoy_http_callbacks bridge_callbacks) {
  post([this, new_stream_handle, bridge_callbacks]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream{new DirectStream(new_stream_handle, *this)};
    direct_stream->callbacks_ =
        std::make_unique<DirectStreamCallbacks>(*direct_stream, bridge_callbacks, *this);

    // Only the initial setting of the api_listener_ is guarded.
    //
    // Note: streams created by Envoy Mobile are tagged as is_internally_created. This means that
    // the Http::ConnectionManager _will not_ sanitize headers when creating a stream.
    direct_stream->request_decoder_ =
        &TS_UNCHECKED_READ(api_listener_)
             ->newStream(*direct_stream->callbacks_, true /* is_internally_created */);

    streams_.emplace(new_stream_handle, std::move(direct_stream));
    ENVOY_LOG(debug, "[S{}] start stream", new_stream_handle);
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendHeaders(envoy_stream_t stream, envoy_headers headers,
                                       bool end_stream) {
  post([this, stream, headers, end_stream]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      RequestHeaderMapPtr internal_headers = Utility::toRequestHeaders(headers);
      setDestinationCluster(*internal_headers);
      // Set the x-forwarded-proto header to https because Envoy Mobile only has clusters with TLS
      // enabled. This is done here because the ApiListener's synthetic connection would make the
      // Http::ConnectionManager set the scheme to http otherwise. In the future we might want to
      // configure the connection instead of setting the header here.
      // https://github.com/envoyproxy/envoy/issues/10291
      //
      // Setting this header is also currently important because Envoy Mobile starts stream with the
      // ApiListener setting the is_internally_created bool to true. This means the
      // Http::ConnectionManager *will not* mutate Envoy Mobile's request headers. One of the
      // mutations done is adding the x-forwarded-proto header if not present. Unfortunately, the
      // router relies on the present of this header to determine if it should provided a route for
      // a request here:
      // https://github.com/envoyproxy/envoy/blob/c9e3b9d2c453c7fe56a0e3615f0c742ac0d5e768/source/common/router/config_impl.cc#L1091-L1096
      internal_headers->setReferenceForwardedProto(Headers::get().SchemeValues.Https);
      ENVOY_LOG(debug, "[S{}] request headers for stream (end_stream={}):\n{}", stream, end_stream,
                *internal_headers);
      direct_stream->request_decoder_->decodeHeaders(std::move(internal_headers), end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendData(envoy_stream_t stream, envoy_data data, bool end_stream) {
  post([this, stream, data, end_stream]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      // The buffer is moved internally, in a synchronous fashion, so we don't need the lifetime
      // of the InstancePtr to outlive this function call.
      Buffer::InstancePtr buf = Buffer::Utility::toInternalData(data);

      ENVOY_LOG(debug, "[S{}] request data for stream (length={} end_stream={})\n", stream,
                data.length, end_stream);
      direct_stream->request_decoder_->decodeData(*buf, end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendMetadata(envoy_stream_t, envoy_headers) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

envoy_status_t Dispatcher::sendTrailers(envoy_stream_t stream, envoy_headers trailers) {
  post([this, stream, trailers]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      RequestTrailerMapPtr internal_trailers = Utility::toRequestTrailers(trailers);
      ENVOY_LOG(debug, "[S{}] request trailers for stream:\n{}", stream, *internal_trailers);
      direct_stream->request_decoder_->decodeTrailers(std::move(internal_trailers));
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::cancelStream(envoy_stream_t stream) {
  post([this, stream]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    if (direct_stream) {
      removeStream(direct_stream->stream_handle_);

      // Testing hook.
      synchronizer_.syncPoint("dispatch_on_cancel");
      direct_stream->callbacks_->onCancel();

      // The runResetCallbacks call synchronously causes Envoy to defer delete the HCM's
      // ActiveStream. We have some concern that this could potentially race a terminal callback
      // scheduled on the same iteration of the event loop. If we see violations in the callback
      // assertions checking stream presence, this is a likely potential culprit. However, it's
      // plausible that upstream guards will protect us here, given that Envoy allows streams to be
      // reset from a wide variety of contexts without apparent issue.
      direct_stream->runResetCallbacks(StreamResetReason::RemoteReset);
    }
  });
  return ENVOY_SUCCESS;
}

const DispatcherStats& Dispatcher::stats() const {
  // Only the initial setting of the api_listener_ is guarded.
  // By the time the Http::Dispatcher is using its stats ready must have been called.
  ASSERT(TS_UNCHECKED_READ(stats_).has_value());
  return TS_UNCHECKED_READ(stats_).value();
}

Dispatcher::DirectStreamSharedPtr Dispatcher::getStream(envoy_stream_t stream) {
  auto direct_stream_pair_it = streams_.find(stream);
  // Returning will copy the shared_ptr and increase the ref count.
  return (direct_stream_pair_it != streams_.end()) ? direct_stream_pair_it->second : nullptr;
}

void Dispatcher::removeStream(envoy_stream_t stream_handle) {
  RELEASE_ASSERT(TS_UNCHECKED_READ(event_dispatcher_)->isThreadSafe(),
                 "stream removeStream must be performed on the event_dispatcher_'s thread.");
  Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream_handle);
  RELEASE_ASSERT(direct_stream,
                 "removeStream is a private method that is only called with stream ids that exist");

  // The DirectStream should live through synchronous code that already has a reference to it.
  // Hence why it is scheduled for deferred deletion. If this was all that was needed then it
  // would be sufficient to return a shared_ptr in getStream. However, deferred deletion is still
  // required because in Dispatcher::resetStream the DirectStream needs to live for as long and
  // the HCM's ActiveStream lives. Hence its deletion needs to live beyond the synchronous code in
  // Dispatcher::resetStream.
  auto direct_stream_wrapper = std::make_unique<DirectStreamWrapper>(std::move(direct_stream));
  TS_UNCHECKED_READ(event_dispatcher_)->deferredDelete(std::move(direct_stream_wrapper));
  // However, the entry in the map should not exist after removeStream.
  // Hence why it is synchronously erased from the streams map.
  size_t erased = streams_.erase(stream_handle);
  ASSERT(erased == 1, "removeStream should always remove one entry from the streams map");
  ENVOY_LOG(debug, "[S{}] erased stream from streams container", stream_handle);
}

namespace {
const LowerCaseString ClusterHeader{"x-envoy-mobile-cluster"};
const std::string BaseCluster = "base";
const std::string BaseWlanCluster = "base_wlan";
const std::string BaseWwanCluster = "base_wwan";
const LowerCaseString H2UpstreamHeader{"x-envoy-mobile-upstream-protocol"};
const std::string H2Suffix = "_h2";
const std::string BaseClusterH2 = BaseCluster + H2Suffix;
const std::string BaseWlanClusterH2 = BaseWlanCluster + H2Suffix;
const std::string BaseWwanClusterH2 = BaseWwanCluster + H2Suffix;
} // namespace

void Dispatcher::setDestinationCluster(HeaderMap& headers) {

  // Determine upstream protocol. Use http2 if selected for explicitly, otherwise (any other value,
  // absence of value) select http1.
  // TODO(junr03): once http3 is available this would probably benefit from some sort of struct that
  // maps to appropriate cluster names. However, with only two options (http1/2) this suffices.
  bool use_h2_cluster{};
  const HeaderEntry* entry = headers.get(H2UpstreamHeader);
  if (entry) {
    if (entry->value() == "http2") {
      use_h2_cluster = true;
    } else {
      ASSERT(entry->value() == "http1",
             fmt::format("using unsupported protocol version {}", entry->value().getStringView()));
    }
    headers.remove(H2UpstreamHeader);
  }

  switch (preferred_network_.load()) {
  case ENVOY_NET_WLAN:
    headers.addReference(ClusterHeader, use_h2_cluster ? BaseWlanClusterH2 : BaseWlanCluster);
    break;
  case ENVOY_NET_WWAN:
    headers.addReference(ClusterHeader, use_h2_cluster ? BaseWwanClusterH2 : BaseWwanCluster);
    break;
  case ENVOY_NET_GENERIC:
  default:
    headers.addReference(ClusterHeader, use_h2_cluster ? BaseClusterH2 : BaseCluster);
  }
}

} // namespace Http
} // namespace Envoy
