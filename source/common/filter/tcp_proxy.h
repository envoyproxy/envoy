#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/request_info_impl.h"
#include "common/common/logger.h"
#include "common/network/cidr_range.h"
#include "common/network/filter_impl.h"
#include "common/network/utility.h"

#include "api/filter/network/tcp_proxy.pb.h"

namespace Envoy {
namespace Filter {

/**
 * All tcp proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TCP_PROXY_STATS(COUNTER, GAUGE)                                                        \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  GAUGE  (downstream_cx_rx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  GAUGE  (downstream_cx_tx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_no_route)                                                                  \
  COUNTER(downstream_flow_control_paused_reading_total)                                            \
  COUNTER(downstream_flow_control_resumed_reading_total)
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
  TcpProxyConfig(const envoy::api::v2::filter::network::TcpProxy& config,
                 Server::Configuration::FactoryContext& context);

  /**
   * Find out which cluster an upstream connection should be opened to based on the
   * parameters of a downstream connection.
   * @param connection supplies the parameters of the downstream connection for
   * which the proxy needs to open the corresponding upstream.
   * @return the cluster name to be used for the upstream connection.
   * If no route applies, returns the empty string.
   */
  const std::string& getRouteFromEntries(Network::Connection& connection);

  const TcpProxyStats& stats() { return stats_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() { return access_logs_; }
  uint32_t maxConnectAttempts() const { return max_connect_attempts_; }

private:
  struct Route {
    Route(const envoy::api::v2::filter::network::TcpProxy::DeprecatedV1::TCPRoute& config);

    Network::Address::IpList source_ips_;
    Network::PortRangeList source_port_ranges_;
    Network::Address::IpList destination_ips_;
    Network::PortRangeList destination_port_ranges_;
    std::string cluster_name_;
  };

  static TcpProxyStats generateStats(const std::string& name, Stats::Scope& scope);

  std::vector<Route> routes_;
  const TcpProxyStats stats_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  const uint32_t max_connect_attempts_;
};

typedef std::shared_ptr<TcpProxyConfig> TcpProxyConfigSharedPtr;

/**
 * An implementation of a TCP (L3/L4) proxy. This filter will instantiate a new outgoing TCP
 * connection using the defined load balancing proxy for the configured cluster. All data will
 * be proxied back and forth between the two connections.
 */
class TcpProxy : public Network::ReadFilter,
                 Upstream::LoadBalancerContext,
                 protected Logger::Loggable<Logger::Id::filter> {
public:
  TcpProxy(TcpProxyConfigSharedPtr config, Upstream::ClusterManager& cluster_manager);
  ~TcpProxy();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onNewConnection() override { return initializeUpstreamConnection(); }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override { return {}; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
  const Network::Connection* downstreamConnection() const override {
    return &read_callbacks_->connection();
  }

  // These two functions allow enabling/disabling reads on the upstream and downstream connections.
  // They are called by the Downstream/Upstream Watermark callbacks to limit buffering.
  void readDisableUpstream(bool disable);
  void readDisableDownstream(bool disable);

protected:
  struct DownstreamCallbacks : public Network::ConnectionCallbacks {
    DownstreamCallbacks(TcpProxy& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onDownstreamEvent(event); }
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    TcpProxy& parent_;
    bool on_high_watermark_called_{false};
  };

  struct UpstreamCallbacks : public Network::ConnectionCallbacks,
                             public Network::ReadFilterBaseImpl {
    UpstreamCallbacks(TcpProxy& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onUpstreamEvent(event); }
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onUpstreamData(data);
      return Network::FilterStatus::StopIteration;
    }

    TcpProxy& parent_;
    bool on_high_watermark_called_{false};
  };

  enum class UpstreamFailureReason {
    CONNECT_FAILED,
    NO_HEALTHY_UPSTREAM,
    RESOURCE_LIMIT_EXCEEDED,
    NO_ROUTE,
  };

  // Callbacks for different error and success states during connection establishment
  virtual const std::string& getUpstreamCluster() {
    return config_->getRouteFromEntries(read_callbacks_->connection());
  }

  virtual void onInitFailure(UpstreamFailureReason) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }

  virtual void onConnectionSuccess() {}

  Network::FilterStatus initializeUpstreamConnection();
  void onConnectTimeout();
  void onDownstreamEvent(Network::ConnectionEvent event);
  void onUpstreamData(Buffer::Instance& data);
  void onUpstreamEvent(Network::ConnectionEvent event);
  void finalizeUpstreamConnectionStats();
  void closeUpstreamConnection();

  TcpProxyConfigSharedPtr config_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::ClientConnectionPtr upstream_connection_;
  DownstreamCallbacks downstream_callbacks_;
  Event::TimerPtr connect_timeout_timer_;
  Stats::TimespanPtr connect_timespan_;
  Stats::TimespanPtr connected_timespan_;
  std::shared_ptr<UpstreamCallbacks> upstream_callbacks_; // shared_ptr required for passing as a
                                                          // read filter.
  AccessLog::RequestInfoImpl request_info_;
  uint32_t connect_attempts_{};
};

} // Filter
} // namespace Envoy
