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
 * An implementation of a WebSocket proxy based on TCP proxy. This filter will instantiate a
 * new outgoing TCP connection using the defined load balancing proxy for the configured cluster.
 * All data will be proxied back and forth between the two connections, without any knowledge of
 * the underlying WebSocket protocol.
 *
 * N.B. This class implements Network::ReadFilter interfaces purely for sake of consistency
 * with TcpProxy filter. WsHandlerImpl it is not used as a network filter in any way.
 */
class WsHandlerImpl : public Filter::TcpProxy {
public:
  WsHandlerImpl(const std::string& cluster_name, Http::HeaderMap& request_headers,
                const Router::RouteEntry* route_entry, StreamDecoderFilterCallbacks& stream,
                Upstream::ClusterManager& cluster_manager);
  ~WsHandlerImpl();

  // Filter::TcpProxy
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

protected:
  // Filter::TcpProxy
  void onConnectTimeout() override;
  void onUpstreamEvent(uint32_t event) override;

private:
  struct NullHttpConnectionCallbacks : public Http::ConnectionCallbacks {
    // Http::ConnectionCallbacks
    void onGoAway() override{};
  };

  const std::string& cluster_name_;
  Http::HeaderMap& request_headers_;
  const Router::RouteEntry* route_entry_;
  Http::StreamDecoderFilterCallbacks& stream_;
  NullHttpConnectionCallbacks http_conn_callbacks_;
};

typedef std::unique_ptr<WsHandlerImpl> WsHandlerImplPtr;

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
