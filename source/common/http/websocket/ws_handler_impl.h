#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/filter/tcp_proxy.h"
#include "common/network/filter_impl.h"

namespace Envoy {
namespace Http {
namespace WebSocket {

/**
 * An implementation of a WebSocket proxy based on TCP proxy. This will be used for
 * handling client connection only after a WebSocket upgrade request succeeds
 * (i.e, it is requested by client and allowed by config). This implementation will
 * instantiate a new outgoing TCP connection for the configured upstream cluster.
 * All data will be proxied back and forth between the two connections, without any
 * knowledge of the underlying WebSocket protocol.
 */
class WsHandlerImpl : public Filter::TcpProxy {
public:
  WsHandlerImpl(Http::HeaderMap& request_headers, const Router::RouteEntry& route_entry,
                WsHandlerCallbacks& callbacks, Upstream::ClusterManager& cluster_manager);
  ~WsHandlerImpl();

  void initializeUpstreamConnection(Network::ReadFilterCallbacks& callbacks);

protected:
  // Filter::TcpProxy
  void onConnectTimeout() override;
  void onUpstreamEvent(uint32_t event) override;

private:
  struct NullHttpConnectionCallbacks : public Http::ConnectionCallbacks {
    // Http::ConnectionCallbacks
    void onGoAway() override{};
  };

  Http::HeaderMap& request_headers_;
  const Router::RouteEntry& route_entry_;
  Http::WsHandlerCallbacks& ws_callbacks_;
  NullHttpConnectionCallbacks http_conn_callbacks_;
};

typedef std::unique_ptr<WsHandlerImpl> WsHandlerImplPtr;

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
