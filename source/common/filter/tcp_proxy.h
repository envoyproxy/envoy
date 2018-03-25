#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/network/cidr_range.h"
#include "common/network/filter_impl.h"
#include "common/network/utility.h"
#include "common/request_info/request_info_impl.h"
#include "common/router/config_impl.h"

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
  COUNTER(downstream_flow_control_resumed_reading_total)                                           \
  COUNTER(idle_timeout)                                                                            \
  COUNTER(upstream_flush_total)                                                                    \
  GAUGE  (upstream_flush_active)
// clang-format on

/**
 * Struct definition for all tcp proxy stats. @see stats_macros.h
 */
struct TcpProxyStats {
  ALL_TCP_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class TcpProxyDrainer;
class TcpProxyUpstreamDrainManager;

/**
 * Filter configuration.
 *
 * This configuration holds a TLS slot, and therefore it must be destructed
 * on the main thread.
 */
class TcpProxyConfig {
public:
  /**
   * Configuration that can be shared and have an arbitrary lifetime safely.
   */
  class SharedConfig {
  public:
    SharedConfig(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& config,
                 Server::Configuration::FactoryContext& context);
    const TcpProxyStats& stats() { return stats_; }
    const absl::optional<std::chrono::milliseconds>& idleTimeout() { return idle_timeout_; }

  private:
    static TcpProxyStats generateStats(Stats::Scope& scope);

    // Hold a Scope for the lifetime of the configuration because connections in
    // the TcpProxyUpstreamDrainManager can live longer than the listener.
    const Stats::ScopePtr stats_scope_;

    const TcpProxyStats stats_;
    absl::optional<std::chrono::milliseconds> idle_timeout_;
  };

  typedef std::shared_ptr<SharedConfig> SharedConfigSharedPtr;

  TcpProxyConfig(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& config,
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

  const TcpProxyStats& stats() { return shared_config_->stats(); }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() { return access_logs_; }
  uint32_t maxConnectAttempts() const { return max_connect_attempts_; }
  const absl::optional<std::chrono::milliseconds>& idleTimeout() {
    return shared_config_->idleTimeout();
  }
  TcpProxyUpstreamDrainManager& drainManager();
  SharedConfigSharedPtr sharedConfig() { return shared_config_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() {
    return cluster_metadata_match_criteria_.get();
  }

private:
  struct Route {
    Route(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy::DeprecatedV1::TCPRoute&
              config);

    Network::Address::IpList source_ips_;
    Network::PortRangeList source_port_ranges_;
    Network::Address::IpList destination_ips_;
    Network::PortRangeList destination_port_ranges_;
    std::string cluster_name_;
  };

  std::vector<Route> routes_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  const uint32_t max_connect_attempts_;
  ThreadLocal::SlotPtr upstream_drain_manager_slot_;
  SharedConfigSharedPtr shared_config_;
  std::unique_ptr<const Router::MetadataMatchCriteria> cluster_metadata_match_criteria_;
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
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return initializeUpstreamConnection(); }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return {}; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    if (config_) {
      return config_->metadataMatchCriteria();
    }

    return nullptr;
  }

  const Network::Connection* downstreamConnection() const override {
    return &read_callbacks_->connection();
  }

  // These two functions allow enabling/disabling reads on the upstream and downstream connections.
  // They are called by the Downstream/Upstream Watermark callbacks to limit buffering.
  void readDisableUpstream(bool disable);
  void readDisableDownstream(bool disable);

  struct UpstreamCallbacks : public Network::ConnectionCallbacks,
                             public Network::ReadFilterBaseImpl {
    UpstreamCallbacks(TcpProxy* parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

    void onBytesSent();
    void onIdleTimeout();
    void drain(TcpProxyDrainer& drainer);

    // Either parent_ or drainer_ will be non-NULL, but never both. This could be
    // logically be represented as a union, but saving one pointer of memory is
    // outweighed by more type safety/better error handling.
    //
    // Parent starts out as non-NULL. If the downstream connection is closed while
    // the upstream connection still has buffered data to flush, drainer_ becomes
    // non-NULL and parent_ is set to NULL.
    TcpProxy* parent_{};
    TcpProxyDrainer* drainer_{};

    bool on_high_watermark_called_{false};
  };

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
  void onUpstreamData(Buffer::Instance& data, bool end_stream);
  void onUpstreamEvent(Network::ConnectionEvent event);
  void finalizeUpstreamConnectionStats();
  void onIdleTimeout();
  void resetIdleTimer();
  void disableIdleTimer();

  TcpProxyConfigSharedPtr config_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::ClientConnectionPtr upstream_connection_;
  DownstreamCallbacks downstream_callbacks_;
  Event::TimerPtr connect_timeout_timer_;
  Event::TimerPtr idle_timer_;
  Stats::TimespanPtr connect_timespan_;
  Stats::TimespanPtr connected_timespan_;
  std::shared_ptr<UpstreamCallbacks> upstream_callbacks_; // shared_ptr required for passing as a
                                                          // read filter.
  RequestInfo::RequestInfoImpl request_info_;
  uint32_t connect_attempts_{};
};

// This class holds ownership of an upstream connection that needs to finish
// flushing, when the downstream connection has been closed. The TcpProxy is
// destroyed when the downstream connection is closed, so moving the upstream
// connection here allows it to finish draining or timeout.
class TcpProxyDrainer : public Event::DeferredDeletable {
public:
  TcpProxyDrainer(TcpProxyUpstreamDrainManager& parent,
                  const TcpProxyConfig::SharedConfigSharedPtr& config,
                  const std::shared_ptr<TcpProxy::UpstreamCallbacks>& callbacks,
                  Network::ClientConnectionPtr&& connection, Event::TimerPtr&& idle_timer,
                  const Upstream::HostDescriptionConstSharedPtr& upstream_host,
                  Stats::TimespanPtr&& connected_timespan);

  void onEvent(Network::ConnectionEvent event);
  void onData(Buffer::Instance& data, bool end_stream);
  void onIdleTimeout();
  void onBytesSent();
  void cancelDrain();

private:
  TcpProxyUpstreamDrainManager& parent_;
  std::shared_ptr<TcpProxy::UpstreamCallbacks> callbacks_;
  Network::ClientConnectionPtr upstream_connection_;
  Event::TimerPtr timer_;
  Stats::TimespanPtr connected_timespan_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  TcpProxyConfig::SharedConfigSharedPtr config_;
};

typedef std::unique_ptr<TcpProxyDrainer> TcpProxyDrainerPtr;

class TcpProxyUpstreamDrainManager : public ThreadLocal::ThreadLocalObject {
public:
  ~TcpProxyUpstreamDrainManager();
  void add(const TcpProxyConfig::SharedConfigSharedPtr& config,
           Network::ClientConnectionPtr&& upstream_connection,
           const std::shared_ptr<TcpProxy::UpstreamCallbacks>& callbacks,
           Event::TimerPtr&& idle_timer,
           const Upstream::HostDescriptionConstSharedPtr& upstream_host,
           Stats::TimespanPtr&& connected_timespan);
  void remove(TcpProxyDrainer& drainer, Event::Dispatcher& dispatcher);

private:
  // This must be a map instead of set because there is no way to move elements
  // out of a set, and these elements get passed to deferredDelete() instead of
  // being deleted in-place. The key and value will always be equal.
  std::unordered_map<TcpProxyDrainer*, TcpProxyDrainerPtr> drainers_;
};

} // Filter
} // namespace Envoy
