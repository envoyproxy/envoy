#pragma once

#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/json/json_loader.h"
#include "common/network/filter_impl.h"

namespace Filter {

/**
 * All tcp proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TCP_PROXY_STATS(COUNTER, GAUGE)                                                        \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  GAUGE  (downstream_cx_rx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  GAUGE  (downstream_cx_tx_bytes_buffered)
// clang-format on

/**
 * Struct definition for all tcp proxy stats. @see stats_macros.h
 */
struct TcpProxyStats {
  ALL_TCP_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Filter configuration.
 */
class TcpProxyConfig {
public:
  TcpProxyConfig(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                 Stats::Store& stats_store);

  const std::string& clusterName() { return cluster_name_; }
  const TcpProxyStats& stats() { return stats_; }

private:
  static TcpProxyStats generateStats(const std::string& name, Stats::Store& store);

  std::string cluster_name_;
  const TcpProxyStats stats_;
};

typedef std::shared_ptr<TcpProxyConfig> TcpProxyConfigPtr;

/**
 * An implementation of a TCP (L3/L4) proxy. This filter will instantiate a new outgoing TCP
 * connection using the defined load balancing proxy for the configured cluster. All data will
 * be proxied back and forth between the two connections.
 */
class TcpProxy : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  TcpProxy(TcpProxyConfigPtr config, Upstream::ClusterManager& cluster_manager);
  ~TcpProxy();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onNewConnection() override { return initializeUpstreamConnection(); }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

private:
  struct DownstreamCallbacks : public Network::ConnectionCallbacks {
    DownstreamCallbacks(TcpProxy& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t event) override { parent_.onDownstreamEvent(event); }

    TcpProxy& parent_;
  };

  struct UpstreamCallbacks : public Network::ConnectionCallbacks,
                             public Network::ReadFilterBaseImpl {
    UpstreamCallbacks(TcpProxy& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t event) override { parent_.onUpstreamEvent(event); }

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onUpstreamData(data);
      return Network::FilterStatus::StopIteration;
    }

    TcpProxy& parent_;
  };

  Network::FilterStatus initializeUpstreamConnection();
  void onConnectTimeout();
  void onDownstreamEvent(uint32_t event);
  void onUpstreamData(Buffer::Instance& data);
  void onUpstreamEvent(uint32_t event);

  TcpProxyConfigPtr config_;
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

} // Filter
