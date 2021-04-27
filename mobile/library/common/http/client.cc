#include "library/common/http/client.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "library/common/buffer/bridge_fragment.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/http/headers.h"
#include "library/common/thread/lock_guard.h"

namespace Envoy {
namespace Http {

/**
 * IMPORTANT: stream closure semantics in envoy mobile depends on the fact that the HCM fires a
 * stream reset when the remote side of the stream is closed but the local side remains open.
 * In other words the HCM (like the rest of Envoy) dissallows locally half-open streams.
 * If this changes in Envoy, this file will need to change as well.
 * For implementation details @see Client::DirectStreamCallbacks::closeRemote.
 */

Client::DirectStreamCallbacks::DirectStreamCallbacks(DirectStream& direct_stream,
                                                     envoy_http_callbacks bridge_callbacks,
                                                     Client& http_client)
    : direct_stream_(direct_stream), bridge_callbacks_(bridge_callbacks),
      http_client_(http_client) {}

void Client::DirectStreamCallbacks::encodeHeaders(const ResponseHeaderMap& headers,
                                                  bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);

  ASSERT(http_client_.getStream(direct_stream_.stream_handle_));
  if (end_stream) {
    closeStream();
  }

  uint64_t response_status = Utility::getResponseStatus(headers);

  // Presence of internal error header indicates an error that should be surfaced as an
  // error callback (rather than an HTTP response).
  const auto error_code_header = headers.get(InternalHeaders::get().ErrorCode);
  if (!error_code_header.empty()) {
    envoy_error_code_t error_code;
    bool check = absl::SimpleAtoi(error_code_header[0]->value().getStringView(), &error_code);
    RELEASE_ASSERT(
        check, fmt::format("[S{}] parse error reading error code", direct_stream_.stream_handle_));
    error_code_ = error_code;

    const auto error_message_header = headers.get(InternalHeaders::get().ErrorMessage);
    if (!error_message_header.empty()) {
      error_message_ =
          Data::Utility::copyToBridgeData(error_message_header[0]->value().getStringView());
    }

    uint32_t attempt_count;
    if (headers.EnvoyAttemptCount() &&
        absl::SimpleAtoi(headers.EnvoyAttemptCount()->value().getStringView(), &attempt_count)) {
      error_attempt_count_ = attempt_count;
    }

    if (end_stream) {
      onError();
    }
    return;
  }

  // Track success for later bookkeeping (stream could still be reset).
  success_ = CodeUtility::is2xx(response_status);

  // Testing hook.
  http_client_.synchronizer_.syncPoint("dispatch_encode_headers");

  ENVOY_LOG(debug, "[S{}] dispatching to platform response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);
  bridge_callbacks_.on_headers(Utility::toBridgeHeaders(headers), end_stream,
                               bridge_callbacks_.context);
  if (end_stream) {
    onComplete();
  }
}

void Client::DirectStreamCallbacks::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);

  ASSERT(http_client_.getStream(direct_stream_.stream_handle_));
  if (end_stream) {
    closeStream();
  }

  if (error_code_) {
    onError();
    return;
  }

  // Testing hook.
  if (end_stream) {
    http_client_.synchronizer_.syncPoint("dispatch_encode_final_data");
  }

  ENVOY_LOG(debug,
            "[S{}] dispatching to platform response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);
  bridge_callbacks_.on_data(Data::Utility::toBridgeData(data), end_stream,
                            bridge_callbacks_.context);
  if (end_stream) {
    onComplete();
  }
}

void Client::DirectStreamCallbacks::encodeTrailers(const ResponseTrailerMap& trailers) {
  ENVOY_LOG(debug, "[S{}] response trailers for stream:\n{}", direct_stream_.stream_handle_,
            trailers);

  ASSERT(http_client_.getStream(direct_stream_.stream_handle_));
  closeStream(); // Trailers always indicate the end of the stream.

  ENVOY_LOG(debug, "[S{}] dispatching to platform response trailers for stream:\n{}",
            direct_stream_.stream_handle_, trailers);
  bridge_callbacks_.on_trailers(Utility::toBridgeHeaders(trailers), bridge_callbacks_.context);
  onComplete();
}

void Client::DirectStreamCallbacks::closeStream() {
  // Envoy itself does not currently allow half-open streams where the local half is open
  // but the remote half is closed. Note that if local is open, Envoy will reset the stream.
  http_client_.removeStream(direct_stream_.stream_handle_);
}

void Client::DirectStreamCallbacks::onComplete() {
  ENVOY_LOG(debug, "[S{}] complete stream (success={})", direct_stream_.stream_handle_, success_);
  if (success_) {
    http_client_.stats().stream_success_.inc();
  } else {
    http_client_.stats().stream_failure_.inc();
  }
  bridge_callbacks_.on_complete(bridge_callbacks_.context);
}

void Client::DirectStreamCallbacks::onError() {
  ENVOY_LOG(debug, "[S{}] remote reset stream", direct_stream_.stream_handle_);

  // The stream should no longer be preset in the map, because onError() was either called from a
  // terminal callback that mapped to an error or it was called in response to a resetStream().
  ASSERT(!http_client_.getStream(direct_stream_.stream_handle_));
  envoy_error_code_t code = error_code_.value_or(ENVOY_STREAM_RESET);
  envoy_data message = error_message_.value_or(envoy_nodata);
  int32_t attempt_count = error_attempt_count_.value_or(-1);

  // Testing hook.
  http_client_.synchronizer_.syncPoint("dispatch_on_error");

  ENVOY_LOG(debug, "[S{}] dispatching to platform remote reset stream",
            direct_stream_.stream_handle_);
  http_client_.stats().stream_failure_.inc();
  bridge_callbacks_.on_error({code, message, attempt_count}, bridge_callbacks_.context);
}

void Client::DirectStreamCallbacks::onCancel() {
  ENVOY_LOG(debug, "[S{}] dispatching to platform cancel stream", direct_stream_.stream_handle_);
  http_client_.stats().stream_cancel_.inc();
  bridge_callbacks_.on_cancel(bridge_callbacks_.context);
}

Client::DirectStream::DirectStream(envoy_stream_t stream_handle, Client& http_client)
    : stream_handle_(stream_handle), parent_(http_client) {}

Client::DirectStream::~DirectStream() { ENVOY_LOG(debug, "[S{}] destroy stream", stream_handle_); }

void Client::DirectStream::resetStream(StreamResetReason reason) {
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
  callbacks_->onError();
}

envoy_status_t Client::startStream(envoy_stream_t new_stream_handle,
                                   envoy_http_callbacks bridge_callbacks) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream{new DirectStream(new_stream_handle, *this)};
  direct_stream->callbacks_ =
      std::make_unique<DirectStreamCallbacks>(*direct_stream, bridge_callbacks, *this);

  // Note: streams created by Envoy Mobile are tagged as is_internally_created. This means that
  // the Http::ConnectionManager _will not_ sanitize headers when creating a stream.
  direct_stream->request_decoder_ =
      &api_listener_.newStream(*direct_stream->callbacks_, true /* is_internally_created */);

  streams_.emplace(new_stream_handle, std::move(direct_stream));
  ENVOY_LOG(debug, "[S{}] start stream", new_stream_handle);

  return ENVOY_SUCCESS;
}

envoy_status_t Client::sendHeaders(envoy_stream_t stream, envoy_headers headers, bool end_stream) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream = getStream(stream);
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

  return ENVOY_SUCCESS;
}

envoy_status_t Client::sendData(envoy_stream_t stream, envoy_data data, bool end_stream) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream = getStream(stream);
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
    Buffer::InstancePtr buf = Data::Utility::toInternalData(data);

    ENVOY_LOG(debug, "[S{}] request data for stream (length={} end_stream={})\n", stream,
              data.length, end_stream);
    direct_stream->request_decoder_->decodeData(*buf, end_stream);
  }

  return ENVOY_SUCCESS;
}

envoy_status_t Client::sendMetadata(envoy_stream_t, envoy_headers) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

envoy_status_t Client::sendTrailers(envoy_stream_t stream, envoy_headers trailers) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream = getStream(stream);
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

  return ENVOY_SUCCESS;
}

envoy_status_t Client::cancelStream(envoy_stream_t stream) {
  ASSERT(dispatcher_.isThreadSafe());
  Client::DirectStreamSharedPtr direct_stream = getStream(stream);
  if (direct_stream) {
    removeStream(direct_stream->stream_handle_);

    // Testing hook.
    synchronizer_.syncPoint("dispatch_on_cancel");
    direct_stream->callbacks_->onCancel();

    // Since https://github.com/envoyproxy/envoy/pull/13052, the connection manager expects that
    // response code details are set on all possible paths for streams.
    direct_stream->setResponseDetails(getCancelDetails());

    // The runResetCallbacks call synchronously causes Envoy to defer delete the HCM's
    // ActiveStream. We have some concern that this could potentially race a terminal callback
    // scheduled on the same iteration of the event loop. If we see violations in the callback
    // assertions checking stream presence, this is a likely potential culprit. However, it's
    // plausible that upstream guards will protect us here, given that Envoy allows streams to be
    // reset from a wide variety of contexts without apparent issue.
    direct_stream->runResetCallbacks(StreamResetReason::RemoteReset);
  }
  return ENVOY_SUCCESS;
}

const HttpClientStats& Client::stats() const { return stats_; }

Client::DirectStreamSharedPtr Client::getStream(envoy_stream_t stream) {
  auto direct_stream_pair_it = streams_.find(stream);
  // Returning will copy the shared_ptr and increase the ref count.
  return (direct_stream_pair_it != streams_.end()) ? direct_stream_pair_it->second : nullptr;
}

void Client::removeStream(envoy_stream_t stream_handle) {
  RELEASE_ASSERT(
      dispatcher_.isThreadSafe(),
      fmt::format("[S{}] stream removeStream must be performed on the dispatcher_'s thread.",
                  stream_handle));
  Client::DirectStreamSharedPtr direct_stream = getStream(stream_handle);
  RELEASE_ASSERT(
      direct_stream,
      fmt::format(
          "[S{}] removeStream is a private method that is only called with stream ids that exist",
          stream_handle));

  // The DirectStream should live through synchronous code that already has a reference to it.
  // Hence why it is scheduled for deferred deletion. If this was all that was needed then it
  // would be sufficient to return a shared_ptr in getStream. However, deferred deletion is still
  // required because in Client::resetStream the DirectStream needs to live for as long and
  // the HCM's ActiveStream lives. Hence its deletion needs to live beyond the synchronous code in
  // Client::resetStream.
  auto direct_stream_wrapper = std::make_unique<DirectStreamWrapper>(std::move(direct_stream));
  dispatcher_.deferredDelete(std::move(direct_stream_wrapper));
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

void Client::setDestinationCluster(HeaderMap& headers) {

  // Determine upstream protocol. Use http2 if selected for explicitly, otherwise (any other value,
  // absence of value) select http1.
  // TODO(junr03): once http3 is available this would probably benefit from some sort of struct that
  // maps to appropriate cluster names. However, with only two options (http1/2) this suffices.
  bool use_h2_cluster{};
  auto get_result = headers.get(H2UpstreamHeader);
  if (!get_result.empty()) {
    ASSERT(get_result.size() == 1);
    const auto value = get_result[0]->value().getStringView();
    if (value == "http2") {
      use_h2_cluster = true;
    } else {
      ASSERT(value == "http1", fmt::format("using unsupported protocol version {}", value));
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
