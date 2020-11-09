#include "test/integration/upstreams/per_host_upstream_request.h"

#include "common/router/router.h"

using Envoy::Router::GenericConnectionPoolCallbacks;

namespace Envoy {

void PerHostHttpConnPool::newStream(GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  conn_pool_stream_handle_ = conn_pool_->newStream(callbacks->upstreamToDownstream(), *this);
}

bool PerHostHttpConnPool::cancelAnyPendingStream() {
  if (conn_pool_stream_handle_) {
    conn_pool_stream_handle_->cancel(ConnectionPool::CancelPolicy::Default);
    conn_pool_stream_handle_ = nullptr;
    return true;
  }
  return false;
}

absl::optional<Envoy::Http::Protocol> PerHostHttpConnPool::protocol() const {
  return conn_pool_->protocol();
}

void PerHostHttpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                        absl::string_view transport_failure_reason,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_stream_handle_ = nullptr;
  callbacks_->onPoolFailure(reason, transport_failure_reason, host);
}

void PerHostHttpConnPool::onPoolReady(Envoy::Http::RequestEncoder& request_encoder,
                                      Upstream::HostDescriptionConstSharedPtr host,
                                      const StreamInfo::StreamInfo& info) {
  conn_pool_stream_handle_ = nullptr;
  auto upstream = std::make_unique<PerHostHttpUpstream>(callbacks_->upstreamToDownstream(),
                                                        &request_encoder, host);
  callbacks_->onPoolReady(std::move(upstream), host,
                          request_encoder.getStream().connectionLocalAddress(), info);
}

} // namespace Envoy