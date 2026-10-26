#include "source/extensions/upstreams/http/http/upstream_request.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/router/router.h"
#include "source/common/runtime/runtime_features.h"

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
      pool_data_.value().newStream(callbacks->upstreamToDownstream(), *this,
                                   callbacks->upstreamToDownstream().upstreamStreamOptions());
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

void HttpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason,
                                 Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_stream_handle_ = nullptr;
  callbacks_->onPoolFailure(reason, transport_failure_reason, host);
}

void HttpConnPool::onPoolReady(Envoy::Http::RequestEncoder& request_encoder,
                               Upstream::HostDescriptionConstSharedPtr host,
                               StreamInfo::StreamInfo& info,
                               std::optional<Envoy::Http::Protocol> protocol) {
  conn_pool_stream_handle_ = nullptr;
  auto upstream =
      std::make_unique<HttpUpstream>(callbacks_->upstreamToDownstream(), &request_encoder, &info);
  callbacks_->onPoolReady(std::move(upstream), host,
                          request_encoder.getStream().connectionInfoProvider(), info, protocol);
}

namespace {

bool isInternalListenerAddress(const Network::Address::InstanceConstSharedPtr& address) {
  return address != nullptr && address->type() == Network::Address::Type::EnvoyInternal;
}

} // namespace

void HttpUpstream::onResetStream(Envoy::Http::StreamResetReason reason,
                                 absl::string_view transport_failure_reason) {
  // When the connection closes, the codec resets each of its streams without details. For a
  // connection to an internal listener, report the connection's transport failure reason instead,
  // which tells why the internal listener closed the connection. ConnectionImpl::closeSocket()
  // stores it in the connection's stream info before it raises the close event that resets the
  // streams. A connection stores its own transport failure reason as the "downstream" one, even
  // when it is an upstream connection like this one.
  //
  // Other connections are excluded: e.g. a TLS connection may or may not record a failure reason
  // for the same close depending on whether the peer reset the TCP connection, which would make
  // the reset details nondeterministic.
  if (transport_failure_reason.empty() && connection_info_ != nullptr &&
      Envoy::Http::Utility::isConnectionResetReason(reason) &&
      isInternalListenerAddress(connection_info_->downstreamAddressProvider().remoteAddress()) &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.router_reset_with_connection_failure_reason")) {
    transport_failure_reason = connection_info_->downstreamTransportFailureReason();
  }
  upstream_request_.onResetStream(reason, transport_failure_reason);
}

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
