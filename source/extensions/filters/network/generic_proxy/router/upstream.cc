#include "source/extensions/filters/network/generic_proxy/router/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

void SharedRequestManager::appendUpstreamRequest(uint64_t stream_id,
                                                 UpstreamRequestCallbacks* pending_request) {
  pending_requests_[stream_id] = pending_request;
}

void SharedRequestManager::removeUpstreamRequest(uint64_t stream_id) {
  pending_requests_.erase(stream_id);
}

void SharedRequestManager::onConnectionClose(Network::ConnectionEvent event) {
  while (!pending_requests_.empty()) {
    // Remove then notify.
    auto it = pending_requests_.begin();
    auto cb = it->second;
    pending_requests_.erase(it);
    cb->onConnectionClose(event);
  }
}

void SharedRequestManager::onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                                             absl::optional<StartTime> start_time) {

  const uint64_t stream_id = header_frame->frameFlags().streamId();
  const bool end_stream = header_frame->frameFlags().endStream();

  auto it = pending_requests_.find(stream_id);
  if (it == pending_requests_.end()) {
    ENVOY_LOG(error, "generic proxy: id {} not found for header frame", stream_id);
    return;
  }

  auto cb = it->second;

  // If the response stream is end, remove the callbacks from the map because we
  // no longer need to track the response.
  if (end_stream) {
    pending_requests_.erase(it);
  }

  cb->onDecodingSuccess(std::move(header_frame), std::move(start_time));
}
void SharedRequestManager::onDecodingSuccess(ResponseCommonFramePtr common_frame) {
  const uint64_t stream_id = common_frame->frameFlags().streamId();
  const bool end_stream = common_frame->frameFlags().endStream();

  auto it = pending_requests_.find(stream_id);
  if (it == pending_requests_.end()) {
    ENVOY_LOG(error, "generic proxy: id {} not found for common frame", stream_id);
    return;
  }

  auto cb = it->second;

  // If the response stream is end, remove the callbacks from the map because we
  // no longer need to track the response.
  if (end_stream) {
    pending_requests_.erase(it);
  }

  cb->onDecodingSuccess(std::move(common_frame));
}

void SharedRequestManager::onDecodingFailure(absl::string_view reason) {
  ENVOY_LOG(error, "generic proxy shared encoder decoder: decoding failure ({})", reason);

  // Notify all pending requests that the decoding is failed.
  while (!pending_requests_.empty()) {
    // Remove then notify.
    auto it = pending_requests_.begin();
    auto cb = it->second;
    pending_requests_.erase(it);
    cb->onDecodingFailure(reason);
  }
}

void UniqueRequestManager::appendUpstreamRequest(uint64_t,
                                                 UpstreamRequestCallbacks* pending_request) {
  ASSERT(pending_request_ == nullptr);
  pending_request_ = pending_request;
}

void UniqueRequestManager::removeUpstreamRequest(uint64_t) { pending_request_ = nullptr; }

void UniqueRequestManager::onConnectionClose(Network::ConnectionEvent event) {
  if (pending_request_ != nullptr) {
    // Remove then notify.
    auto cb = pending_request_;
    pending_request_ = nullptr;

    cb->onConnectionClose(event);
  }
}

void UniqueRequestManager::onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                                             absl::optional<StartTime> start_time) {
  if (pending_request_ != nullptr) {
    auto cb = pending_request_;

    // If the response is end, reset the pending request because we no longer need
    // to track the response.
    if (header_frame->frameFlags().endStream()) {
      pending_request_ = nullptr;
    }

    cb->onDecodingSuccess(std::move(header_frame), std::move(start_time));
  }
}
void UniqueRequestManager::onDecodingSuccess(ResponseCommonFramePtr common_frame) {
  if (pending_request_ != nullptr) {
    auto cb = pending_request_;

    // If the response is end, reset the pending request because we no longer need
    // to track the response.
    if (common_frame->frameFlags().endStream()) {
      pending_request_ = nullptr;
    }

    cb->onDecodingSuccess(std::move(common_frame));
  }
}

void UniqueRequestManager::onDecodingFailure(absl::string_view reason) {
  if (pending_request_ != nullptr) {
    // Remove then notify.
    auto cb = pending_request_;
    pending_request_ = nullptr;
    cb->onDecodingFailure(reason);
  }
}

BoundGenericUpstream::BoundGenericUpstream(Envoy::Upstream::TcpPoolData tcp_pool_data,
                                           const CodecFactory& codec_factory,
                                           Network::Connection& downstream_connection)
    : UpstreamBase(std::move(tcp_pool_data), codec_factory),
      downstream_conn_(downstream_connection),
      connection_event_watcher_(
          [this](Network::ConnectionEvent event) { onDownstreamConnectionEvent(event); }) {
  downstream_conn_.addConnectionCallbacks(connection_event_watcher_);
}

void BoundGenericUpstream::onDownstreamConnectionEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    // The event should be handled first by the generic proxy first. So all pending
    // requests will be cleaned up by the downstream connection close event.
    ASSERT(pending_requests_.empty());
    ASSERT(encoder_decoder_ == nullptr || encoder_decoder_->requestsSize() == 0);

    // Close upstream connection and this will trigger the upstream connection close event.
    cleanUp(true);
  }
}

void BoundGenericUpstream::appendUpstreamRequest(uint64_t stream_id,
                                                 UpstreamRequestCallbacks* pending_request) {

  if (upstream_conn_ok_.has_value()) {
    // Upstream connection is already ready. If the upstream connection is failed then
    // all pending requests will be reset and no new upstream request will be created.
    if (!upstream_conn_ok_.value()) {
      return;
    }

    ASSERT(encoder_decoder_ != nullptr);

    if (encoder_decoder_->containsRequest(stream_id)) {
      ENVOY_LOG(error, "generic proxy upstream: id {} already registered for waiting responses",
                stream_id);
      downstream_conn_.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

    encoder_decoder_->appendUpstreamRequest(stream_id, pending_request);
    pending_request->onUpstreamSuccess();
  } else {
    ASSERT(encoder_decoder_ == nullptr);

    if (pending_requests_.contains(stream_id)) {
      ENVOY_LOG(error, "generic proxy upstream: id {} already registered for waiting upstream",
                stream_id);
      downstream_conn_.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

    pending_requests_[stream_id] = pending_request;

    // Try to initialize the upstream connection after there is at least one pending request.
    // If the upstream connection is already initialized, this is a no-op.
    tryInitialize();
  }
}

void BoundGenericUpstream::removeUpstreamRequest(uint64_t stream_id) {
  pending_requests_.erase(stream_id);
  if (encoder_decoder_ != nullptr) {
    encoder_decoder_->removeUpstreamRequest(stream_id);
  }
}

void BoundGenericUpstream::cleanUp(bool close_connection) {
  // Shared upstream manager never release the connection back to the pool
  // because the connection is bound to the downstream connection and is shared by
  // multiple requests.
  if (close_connection) {
    // Only actually do the cleanup when we want to close the connection.
    BoundGenericUpstreamBase::cleanUp(true);
  }
}

void BoundGenericUpstream::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    if (encoder_decoder_ != nullptr) {
      encoder_decoder_->onConnectionClose(event);
    }

    // If the downstream connection is not closed, close it.
    downstream_conn_.close(Network::ConnectionCloseType::FlushWrite);
  }
}

void BoundGenericUpstream::onUpstreamSuccess() {
  // This should be called only once and all pending requests should be notified. After this is
  // called, the upstream connection is ready and new upstream requests should be notified directly.

  ASSERT(!upstream_conn_ok_.has_value());
  // encoder_decoder_ should be initialized after the upstream connection is ready and before the
  // onUpstreamSuccess() is called.
  ASSERT(encoder_decoder_ != nullptr);
  upstream_conn_ok_ = true;

  while (!pending_requests_.empty()) {
    auto it = pending_requests_.begin();
    auto cb = it->second;

    // Insert it to the waiting response list and remove it from the waiting upstream list.
    encoder_decoder_->appendUpstreamRequest(it->first, cb);
    pending_requests_.erase(it);

    // Notify the upstream request that the upstream connection is ready and request could be sent.
    cb->onUpstreamSuccess();
  }
}

void BoundGenericUpstream::onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_reason) {
  // This should be called only once and all pending requests should be notified.
  // Then the downstream connection will be closed.

  ASSERT(!upstream_conn_ok_.has_value());
  upstream_conn_ok_ = false;

  while (!pending_requests_.empty()) {
    auto it = pending_requests_.begin();
    auto cb = it->second;

    // Remove it from the waiting upstream list.
    pending_requests_.erase(it);

    // Now, notify the upstream request that the upstream connection is failed.
    cb->onUpstreamFailure(reason, transport_reason);
  }

  // If the downstream connection is not closed, close it.
  downstream_conn_.close(Network::ConnectionCloseType::FlushWrite);
}

void OwnedGenericUpstream::appendUpstreamRequest(uint64_t,
                                                 UpstreamRequestCallbacks* pending_request) {
  ASSERT(upstream_request_ == nullptr);
  upstream_request_ = pending_request;
  tryInitialize();
}

void OwnedGenericUpstream::removeUpstreamRequest(uint64_t) {
  if (encoder_decoder_ != nullptr) {
    encoder_decoder_->removeUpstreamRequest({});
  }
  upstream_request_ = nullptr;
}

void OwnedGenericUpstream::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    if (encoder_decoder_ != nullptr) {
      encoder_decoder_->onConnectionClose(event);
    }
  }
}

void OwnedGenericUpstream::onUpstreamSuccess() {
  ASSERT(upstream_request_ != nullptr);
  ASSERT(encoder_decoder_ != nullptr);

  auto upstream_request = upstream_request_;
  upstream_request_ = nullptr;

  encoder_decoder_->appendUpstreamRequest({}, upstream_request);
  upstream_request->onUpstreamSuccess();
}

void OwnedGenericUpstream::onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_failure_reason) {
  ASSERT(upstream_request_ != nullptr);
  auto upstream_request = upstream_request_;
  upstream_request_ = nullptr;

  upstream_request->onUpstreamFailure(reason, transport_failure_reason);
}

GenericUpstreamSharedPtr ProdGenericUpstreamFactory::createGenericUpstream(
    Upstream::ThreadLocalCluster& cluster, Upstream::LoadBalancerContext* context,
    Network::Connection& downstream_conn, const CodecFactory& codec_factory, bool bound) const {

  if (bound) {
    auto* bound_upstream =
        downstream_conn.streamInfo().filterState()->getDataMutable<BoundGenericUpstream>(
            RouterFilterName);
    if (bound_upstream == nullptr) {
      // The upstream connection is not bound yet and create a new bound upstream connection.
      auto pool_data = cluster.tcpConnPool(Upstream::ResourcePriority::Default, context);
      if (!pool_data.has_value()) {
        return nullptr;
      }
      auto new_bound_upstream = std::make_shared<BoundGenericUpstream>(
          std::move(pool_data.value()), codec_factory, downstream_conn);
      bound_upstream = new_bound_upstream.get();
      downstream_conn.streamInfo().filterState()->setData(
          RouterFilterName, std::move(new_bound_upstream),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    }
    return bound_upstream->shared_from_this();
  } else {
    // Upstream connection binding is disabled and create a new upstream connection.
    auto pool_data = cluster.tcpConnPool(Upstream::ResourcePriority::Default, context);
    if (!pool_data.has_value()) {
      return nullptr;
    }
    return std::make_shared<OwnedGenericUpstream>(std::move(pool_data.value()), codec_factory);
  }
}

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
