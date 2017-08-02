#pragma once

#include <string>

#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/websocket.h"
#include "envoy/network/filter.h"
#include "envoy/router/router.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/filter/tcp_proxy.h"

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
  ~WsHandlerImpl(){};

  // Filter::TcpProxy
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  void initializeUpstreamHelperCallbacks() override {
    upstream_helper_callbacks_.reset(new WsUpstreamHelperCallbacks(*this));
  }

protected:
  struct WsUpstreamHelperCallbacks : public UpstreamHelperCallbacks {
    WsUpstreamHelperCallbacks(WsHandlerImpl& parent) : parent_(parent) {}

    // UpstreamHelperBase
    const std::string& getUpstreamCluster() override { return parent_.route_entry_.clusterName(); }

    void onInitFailure() override;
    void onUpstreamHostReady() override;
    void onConnectTimeout() override;
    void onConnectionFailure() override;
    void onConnectionSuccess() override;

    WsHandlerImpl& parent_;
  };

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
