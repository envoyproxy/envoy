#include "extensions/upstreams/http/http/upstream_request.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/router/router.h"

using Envoy::Router::GenericConnectionPoolCallbacks;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

void HttpConnPool::newStream(GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  // It's possible for a reset to happen inline within the newStream() call. In this case, we
  // might get deleted inline as well. Only write the returned handle out if it is not nullptr to
  // deal with this case.
  Envoy::Http::ConnectionPool::Cancellable* handle =
      conn_pool_->newStream(callbacks->upstreamToDownstream(), *this);
  if (handle) {
    conn_pool_stream_handle_ = handle;
  }
}

bool HttpConnPool::cancelAnyPendingStream() {
  if (conn_pool_stream_handle_) {
    conn_pool_stream_handle_->cancel(ConnectionPool::CancelPolicy::Default);
    conn_pool_stream_handle_ = nullptr;
    return true;
  }
  return false;
}

absl::optional<Envoy::Http::Protocol> HttpConnPool::protocol() const {
  return conn_pool_->protocol();
}

void HttpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason,
                                 Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_stream_handle_ = nullptr;
  callbacks_->onPoolFailure(reason, transport_failure_reason, host);
}

void HttpConnPool::onPoolReady(Envoy::Http::RequestEncoder& request_encoder,
                               Upstream::HostDescriptionConstSharedPtr host,
                               const StreamInfo::StreamInfo& info) {
  conn_pool_stream_handle_ = nullptr;
  auto upstream =
      std::make_unique<HttpUpstream>(callbacks_->upstreamToDownstream(), &request_encoder);
  callbacks_->onPoolReady(std::move(upstream), host,
                          request_encoder.getStream().connectionLocalAddress(), info);
}

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
