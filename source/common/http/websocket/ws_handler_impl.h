#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/network/filter_impl.h"

namespace Envoy {
namespace Http {
namespace WebSocket {

/**
 * An implementation of a WebSocket proxy based on TCP proxy. This filter will instantiate a
 * new outgoing TCP connection using the defined load balancing proxy for the configured cluster.
 * All data will be proxied back and forth between the two connections, without any knowledge of
 * the underlying WebSocket protocol.
 */
class WsHandlerImpl : Logger::Loggable<Logger::Id::websocket> {
public:
  WsHandlerImpl(const std::string& cluster_name, Http::HeaderMap& request_headers,
                StreamDecoderFilterCallbacks& stream, Upstream::ClusterManager& cluster_manager);
  ~WsHandlerImpl();

  Network::FilterStatus onData(Buffer::Instance& data);
  void initializeUpstreamConnection(Network::ReadFilterCallbacks& callbacks,
                                    const Router::RouteEntry* route_entry);

private:
  struct DownstreamCallbacks : public Network::ConnectionCallbacks {
    DownstreamCallbacks(WsHandlerImpl& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t event) override { parent_.onDownstreamEvent(event); }

    WsHandlerImpl& parent_;
  };

  struct UpstreamCallbacks : public Network::ConnectionCallbacks,
                             public Http::ConnectionCallbacks,
                             public Network::ReadFilterBaseImpl {
    UpstreamCallbacks(WsHandlerImpl& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t event) override { parent_.onUpstreamEvent(event); }

    // Http::ConnectionCallbacks
    void onGoAway() override{};

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onUpstreamData(data);
      return Network::FilterStatus::StopIteration;
    }

    WsHandlerImpl& parent_;
  };

  void onConnectTimeout();
  void onDownstreamEvent(uint32_t event);
  void onUpstreamData(Buffer::Instance& data);
  void onUpstreamEvent(uint32_t event);

  const std::string& cluster_name_;
  Http::HeaderMap& request_headers_;
  Http::StreamDecoderFilterCallbacks& stream_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::ClientConnectionPtr upstream_connection_;
  DownstreamCallbacks downstream_callbacks_;
  Event::TimerPtr connect_timeout_timer_;
  Stats::TimespanPtr connect_timespan_;
  Stats::TimespanPtr connected_timespan_;
  std::shared_ptr<UpstreamCallbacks> upstream_callbacks_; // shared_ptr required for passing as a
  // read filter.
};

typedef std::unique_ptr<WsHandlerImpl> WsHandlerImplPtr;

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
