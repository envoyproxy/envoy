#include "source/extensions/filters/network/thrift_proxy/router/upstream_request.h"

#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

UpstreamRequest::UpstreamRequest(RequestOwner& parent, Upstream::TcpPoolData& pool_data,
                                 MessageMetadataSharedPtr& metadata, TransportType transport_type,
                                 ProtocolType protocol_type)
    : parent_(parent), conn_pool_data_(pool_data), metadata_(metadata),
      transport_(NamedTransportConfigFactory::getFactory(transport_type).createTransport()),
      protocol_(NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol()),
      request_complete_(false), response_started_(false), response_complete_(false) {}

UpstreamRequest::~UpstreamRequest() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

FilterStatus UpstreamRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_data_.newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    return FilterStatus::StopIteration;
  }

  if (upgrade_response_ != nullptr) {
    // Pause while we wait for an upgrade response.
    return FilterStatus::StopIteration;
  }

  if (upstream_host_ == nullptr) {
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}

void UpstreamRequest::releaseConnection(const bool close) {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    conn_pool_handle_ = nullptr;
  }

  conn_state_ = nullptr;

  // The event triggered by close will also release this connection so clear conn_data_ before
  // closing.
  auto conn_data = std::move(conn_data_);
  if (close && conn_data != nullptr) {
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void UpstreamRequest::resetStream() { releaseConnection(true); }

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reason);
}

void UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  // Only invoke continueDecoding if we'd previously stopped the filter chain.
  bool continue_decoding = conn_pool_handle_ != nullptr;

  onUpstreamHostSelected(host);
  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  conn_data_ = std::move(conn_data);
  conn_data_->addUpstreamCallbacks(parent_.upstreamCallbacks());
  conn_pool_handle_ = nullptr;

  conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  if (conn_state_ == nullptr) {
    conn_data_->setConnectionState(std::make_unique<ThriftConnectionState>());
    conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  }

  if (protocol_->supportsUpgrade()) {
    auto& buffer = parent_.buffer();
    upgrade_response_ = protocol_->attemptUpgrade(*transport_, *conn_state_, buffer);
    if (upgrade_response_ != nullptr) {
      parent_.addSize(buffer.length());
      conn_data_->connection().write(buffer, false);
      return;
    }
  }

  onRequestStart(continue_decoding);
}

void UpstreamRequest::onRequestStart(bool continue_decoding) {
  auto& buffer = parent_.buffer();
  parent_.initProtocolConverter(*protocol_, buffer);

  metadata_->setSequenceId(conn_state_->nextSequenceId());
  parent_.convertMessageBegin(metadata_);

  if (continue_decoding) {
    parent_.continueDecoding();
  }
}

void UpstreamRequest::onRequestComplete() {
  Event::Dispatcher& dispatcher = parent_.dispatcher();
  downstream_request_complete_time_ = dispatcher.timeSource().monotonicTime();
  request_complete_ = true;
}

void UpstreamRequest::onResponseComplete() {
  chargeResponseTiming();
  response_complete_ = true;
  conn_state_ = nullptr;
  conn_data_.reset();
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void UpstreamRequest::onResetStream(ConnectionPool::PoolFailureReason reason) {
  if (metadata_->messageType() == MessageType::Oneway) {
    // For oneway requests, we should not attempt a response. Reset the downstream to signal
    // an error.
    parent_.resetDownstreamConnection();
    return;
  }

  chargeResponseTiming();

  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    parent_.sendLocalReply(AppException(AppExceptionType::InternalError,
                                        "thrift upstream request: too many connections"),
                           true);
    break;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    upstream_host_->outlierDetector().putResult(
        Upstream::Outlier::Result::LocalOriginConnectFailed);
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    parent_.resetDownstreamConnection();
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
  case ConnectionPool::PoolFailureReason::Timeout:
    if (reason == ConnectionPool::PoolFailureReason::Timeout) {
      upstream_host_->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginTimeout);
    } else if (reason == ConnectionPool::PoolFailureReason::RemoteConnectionFailure) {
      upstream_host_->outlierDetector().putResult(
          Upstream::Outlier::Result::LocalOriginConnectFailed);
    }

    // TODO(zuercher): distinguish between these cases where appropriate (particularly timeout)
    if (!response_started_) {
      parent_.sendLocalReply(AppException(AppExceptionType::InternalError,
                                          fmt::format("connection failure '{}'",
                                                      (upstream_host_ != nullptr)
                                                          ? upstream_host_->address()->asString()
                                                          : "to upstream")),
                             true);
      return;
    }

    // Error occurred after a partial response, propagate the reset to the downstream.
    parent_.resetDownstreamConnection();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void UpstreamRequest::chargeResponseTiming() {
  if (charged_response_timing_ || !request_complete_) {
    return;
  }
  charged_response_timing_ = true;
  Event::Dispatcher& dispatcher = parent_.dispatcher();
  const std::chrono::milliseconds response_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);
  parent_.recordResponseDuration(response_time.count(), Stats::Histogram::Unit::Milliseconds);
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
