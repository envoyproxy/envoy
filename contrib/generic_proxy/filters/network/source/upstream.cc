#include "contrib/generic_proxy/filters/network/source/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

UpstreamConnection::~UpstreamConnection() {
  // In case we doesn't clean up the pending connecting request.
  if (tcp_pool_handle_ != nullptr) {
    // Clear the data first.
    auto local_handle = tcp_pool_handle_;
    tcp_pool_handle_ = nullptr;

    local_handle->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

void UpstreamConnection::initialize() {
  if (!initialized_) {
    initialized_ = true;
    newConnection();
  }
}

void UpstreamConnection::cleanUp(bool close_connection) {
  ENVOY_LOG(debug, "generic proxy upstream manager: clean up upstream (close: {})",
            close_connection);

  if (close_connection && owned_conn_data_ != nullptr) {
    ENVOY_LOG(debug, "generic proxy upstream request: close upstream connection");
    ASSERT(tcp_pool_handle_ == nullptr);

    // Clear the data first to avoid re-entering this function in the close callback.
    auto local_data = std::move(owned_conn_data_);
    owned_conn_data_.reset();

    local_data->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  if (tcp_pool_handle_ != nullptr) {
    ENVOY_LOG(debug, "generic proxy upstream manager: cacel upstream connection");
    ASSERT(owned_conn_data_ == nullptr);

    // Clear the data first.
    auto local_handle = tcp_pool_handle_;
    tcp_pool_handle_ = nullptr;

    local_handle->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

void UpstreamConnection::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (data.length() == 0) {
    return;
  }

  client_codec_->decode(data, end_stream);
}

void UpstreamConnection::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                       absl::string_view transport_failure_reason,
                                       Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection failure (host: {})",
            host != nullptr ? host->address()->asStringView() : absl::string_view{});

  tcp_pool_handle_ = nullptr;
  upstream_host_ = std::move(host);

  onPoolFailureImpl(reason, transport_failure_reason);
}

void UpstreamConnection::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                     Upstream::HostDescriptionConstSharedPtr host) {
  ASSERT(host != nullptr);
  ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection ready (host: {})",
            host->address()->asStringView());

  tcp_pool_handle_ = nullptr;
  upstream_host_ = std::move(host);

  owned_conn_data_ = std::move(conn_data);
  owned_conn_data_->addUpstreamCallbacks(*this);

  onPoolSuccessImpl();
}

void UpstreamConnection::onEvent(Network::ConnectionEvent event) { onEventImpl(event); }

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
