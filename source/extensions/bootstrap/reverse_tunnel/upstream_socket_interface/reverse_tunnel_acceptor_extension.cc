#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Static warning flag for reverse tunnel detailed stats activation.
static bool reverse_tunnel_detailed_stats_warning_logged = false;

namespace {

void setStringMetadataField(Protobuf::Struct& metadata, absl::string_view key,
                            absl::string_view value) {
  (*metadata.mutable_fields())[std::string(key)].set_string_value(std::string(value));
}

void setNumberMetadataField(Protobuf::Struct& metadata, absl::string_view key, double value) {
  (*metadata.mutable_fields())[std::string(key)].set_number_value(value);
}

void maybeSetStringFilterState(StreamInfo::FilterState& filter_state, absl::string_view key,
                               absl::string_view value) {
  if (value.empty() || filter_state.hasDataWithName(key)) {
    return;
  }

  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>(value),
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection);
}

void maybeSetUint64FilterState(StreamInfo::FilterState& filter_state, absl::string_view key,
                               uint64_t value) {
  if (filter_state.hasDataWithName(key)) {
    return;
  }

  filter_state.setData(key, std::make_shared<StreamInfo::UInt64AccessorImpl>(value),
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection);
}

absl::string_view socketStateForEvent(absl::string_view event,
                                      const ReverseTunnelLifecycleInfo& lifecycle) {
  if (event == kLifecycleEventSocketHandoff) {
    return kLifecycleSocketStateHandedOff;
  }

  return lifecycle.handed_off_to_upstream ? kLifecycleSocketStateInUse : kLifecycleSocketStateIdle;
}

} // namespace

// UpstreamSocketThreadLocal implementation
UpstreamSocketThreadLocal::UpstreamSocketThreadLocal(Event::Dispatcher& dispatcher,
                                                     ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher),
      socket_manager_(std::make_unique<UpstreamSocketManager>(dispatcher, extension)) {
  // Initialize per-worker aggregate metrics.
  if (extension != nullptr) {
    auto& stats_store = extension->getStatsScope();
    std::string dispatcher_name = dispatcher.name();
    std::string stat_prefix = extension->statPrefix();

    std::string total_clusters_stat_name =
        fmt::format("{}.{}.total_clusters", stat_prefix, dispatcher_name);
    Stats::StatNameManagedStorage total_clusters_stat_name_storage(total_clusters_stat_name,
                                                                   stats_store.symbolTable());
    total_clusters_gauge_ = &stats_store.gaugeFromStatName(
        total_clusters_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);

    std::string total_nodes_stat_name =
        fmt::format("{}.{}.total_nodes", stat_prefix, dispatcher_name);
    Stats::StatNameManagedStorage total_nodes_stat_name_storage(total_nodes_stat_name,
                                                                stats_store.symbolTable());
    total_nodes_gauge_ = &stats_store.gaugeFromStatName(total_nodes_stat_name_storage.statName(),
                                                        Stats::Gauge::ImportMode::NeverImport);
  }
}

ReverseTunnelAcceptorExtension::~ReverseTunnelAcceptorExtension() {
  // Reset the TLS slot before other members (especially access_logs_) are destroyed.
  // Without this, the implicit member destruction order destroys access_logs_ before tls_slot_,
  // and the UpstreamSocketManager destructor (triggered by tls_slot_ teardown) calls
  // emitSyntheticLifecycleLog() which accesses the already-destroyed access_logs_, causing a
  // segfault under GCC.
  tls_slot_.reset();
}

ReverseTunnelAcceptorExtension::ReverseTunnelAcceptorExtension(
    ReverseTunnelAcceptor& sock_interface, Server::Configuration::ServerFactoryContext& context,
    const envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
        UpstreamReverseConnectionSocketInterface& config)
    : Envoy::Network::SocketInterfaceExtension(sock_interface), context_(context),
      socket_interface_(&sock_interface) {
  stat_prefix_ = PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "reverse_tunnel_acceptor");
  const uint32_t cfg_threshold = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, ping_failure_threshold, 3);
  ping_failure_threshold_ = std::max<uint32_t>(1, cfg_threshold);
  enable_detailed_stats_ = config.enable_detailed_stats();
  enable_tenant_isolation_ =
      config.has_enable_tenant_isolation() ? config.enable_tenant_isolation().value() : false;

  Server::GenericFactoryContextImpl generic_context(context_, context_.messageValidationVisitor());
  for (const auto& access_log_config : config.access_log()) {
    access_logs_.push_back(
        AccessLog::AccessLogFactory::fromProto(access_log_config, generic_context));
  }

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension: creating upstream reverse connection socket "
            "interface with stat_prefix: {}, tenant_isolation: {}, access_logs: {}",
            stat_prefix_, enable_tenant_isolation_, access_logs_.size());

  if (config.has_reporter_config()) {
    auto& reporter_factory =
        Config::Utility::getAndCheckFactoryByName<ReverseTunnelReporterFactory>(
            config.reporter_config().name());
    auto reporter_config = Config::Utility::translateAnyToFactoryConfig(
        config.reporter_config().typed_config(), context_.messageValidationVisitor(),
        reporter_factory);

    reporter_ = reporter_factory.createReporter(context, std::move(reporter_config));
  }

  if (socket_interface_ != nullptr) {
    socket_interface_->extension_ = this;
  }
}

void ReverseTunnelAcceptorExtension::emitSyntheticLifecycleLog(
    absl::string_view event, const ReverseTunnelLifecycleInfo& lifecycle, TimeSource& time_source,
    AccessLog::AccessLogType access_log_type, absl::string_view handoff_kind,
    absl::string_view close_reason, const LifecycleLogMetadata& extra_metadata) const {
  if (access_logs_.empty()) {
    return;
  }

  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      lifecycle.local_address, lifecycle.remote_address);
  StreamInfo::StreamInfoImpl stream_info(time_source, connection_info_provider,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  populateLifecycleStreamInfo(stream_info, lifecycle, event, handoff_kind, close_reason,
                              extra_metadata);
  stream_info.onRequestComplete();
  emitLifecycleLog(Formatter::Context(nullptr, nullptr, nullptr, {}, access_log_type), stream_info);
}

void ReverseTunnelAcceptorExtension::emitConnectionLifecycleLog(
    absl::string_view event, StreamInfo::StreamInfo& stream_info,
    const ReverseTunnelLifecycleInfo& lifecycle, AccessLog::AccessLogType access_log_type,
    absl::string_view handoff_kind, absl::string_view close_reason,
    const LifecycleLogMetadata& extra_metadata) const {
  if (access_logs_.empty()) {
    return;
  }

  populateLifecycleStreamInfo(stream_info, lifecycle, event, handoff_kind, close_reason,
                              extra_metadata);
  emitLifecycleLog(Formatter::Context(nullptr, nullptr, nullptr, {}, access_log_type), stream_info);
}

void ReverseTunnelAcceptorExtension::emitLifecycleLog(
    const Formatter::Context& log_context, const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& access_log : access_logs_) {
    access_log->log(log_context, stream_info);
  }
}

void ReverseTunnelAcceptorExtension::populateLifecycleStreamInfo(
    StreamInfo::StreamInfo& stream_info, const ReverseTunnelLifecycleInfo& lifecycle,
    absl::string_view event, absl::string_view handoff_kind, absl::string_view close_reason,
    const LifecycleLogMetadata& extra_metadata) const {
  if (const auto& filter_state = stream_info.filterState(); filter_state != nullptr) {
    maybeSetStringFilterState(*filter_state, kFilterStateNodeId, lifecycle.node_id);
    maybeSetStringFilterState(*filter_state, kFilterStateClusterId, lifecycle.cluster_id);
    maybeSetStringFilterState(*filter_state, kFilterStateTenantId, lifecycle.tenant_id);
    maybeSetStringFilterState(*filter_state, kFilterStateWorker, lifecycle.worker);
    if (lifecycle.fd >= 0) {
      maybeSetUint64FilterState(*filter_state, kFilterStateFd, lifecycle.fd);
    }
  }

  Protobuf::Struct metadata;
  setStringMetadataField(metadata, "event", event);
  setStringMetadataField(metadata, "node_id", lifecycle.node_id);
  setStringMetadataField(metadata, "cluster_id", lifecycle.cluster_id);
  setStringMetadataField(metadata, "tenant_id", lifecycle.tenant_id);
  setStringMetadataField(metadata, "worker", lifecycle.worker);
  setStringMetadataField(metadata, "socket_state", socketStateForEvent(event, lifecycle));
  if (!handoff_kind.empty()) {
    setStringMetadataField(metadata, "handoff_kind", handoff_kind);
  }
  if (!close_reason.empty()) {
    setStringMetadataField(metadata, "close_reason", close_reason);
    stream_info.setConnectionTerminationDetails(close_reason);
  }
  if (lifecycle.fd >= 0) {
    setNumberMetadataField(metadata, "fd", lifecycle.fd);
  }
  for (const auto& [key, value] : extra_metadata) {
    if (!value.empty()) {
      setStringMetadataField(metadata, key, value);
    }
  }

  stream_info.setDynamicMetadata(std::string(kAccessLogMetadataNamespace), metadata);
}

// ReverseTunnelAcceptorExtension implementation
void ReverseTunnelAcceptorExtension::onServerInitialized(Server::Instance&) {
  // Initialize the reporter.
  if (reporter_) {
    reporter_->onServerInitialized();
  }

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension::onServerInitialized: creating thread-local slot.");

  // Set the extension reference in the socket interface.
  if (socket_interface_) {
    socket_interface_->extension_ = this;
  }

  // Create thread-local slot for the dispatcher and socket manager.
  tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  const uint32_t ping_failure_threshold = ping_failure_threshold_;
  const bool enable_tenant_isolation = enable_tenant_isolation_;
  // Set up the thread-local dispatcher and socket manager.
  tls_slot_->set(
      [this, ping_failure_threshold, enable_tenant_isolation](Event::Dispatcher& dispatcher) {
        auto tls = std::make_shared<UpstreamSocketThreadLocal>(dispatcher, this);
        // Propagate configured miss threshold and tenant isolation into the socket manager.
        if (auto* socket_manager = tls->socketManager(); socket_manager != nullptr) {
          socket_manager->setMissThreshold(ping_failure_threshold);
          socket_manager->setTenantIsolationEnabled(enable_tenant_isolation);
        }
        return tls;
      });
}

// Get thread-local registry for the current thread.
UpstreamSocketThreadLocal* ReverseTunnelAcceptorExtension::getLocalRegistry() const {
  if (!tls_slot_) {
    ENVOY_LOG(error, "ReverseTunnelAcceptorExtension::getLocalRegistry(): no thread-local slot.");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelAcceptorExtension::getConnectionStatsSync(std::chrono::milliseconds /* timeout_ms */) {

  ENVOY_LOG(debug, "reverse_tunnel: obtaining reverse connection stats");

  // Get all gauges with the reverse_connections prefix.
  auto connection_stats = getCrossWorkerStatMap();

  std::vector<std::string> connected_nodes;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information.
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract node/cluster information.
      // Format: "<scope>.<stat_prefix>.nodes.<node_id>" or
      // "<scope>.<stat_prefix>.clusters.<cluster_id>".
      std::string nodes_pattern = stat_prefix_ + ".nodes.";
      std::string clusters_pattern = stat_prefix_ + ".clusters.";
      if (stat_name.find(nodes_pattern) != std::string::npos) {
        // Find the position after "<stat_prefix>.nodes.".
        size_t pos = stat_name.find(nodes_pattern);
        if (pos != std::string::npos) {
          std::string node_id = stat_name.substr(pos + nodes_pattern.length());
          connected_nodes.push_back(node_id);
        }
      } else if (stat_name.find(clusters_pattern) != std::string::npos) {
        // Find the position after "<stat_prefix>.clusters.".
        size_t pos = stat_name.find(clusters_pattern);
        if (pos != std::string::npos) {
          std::string cluster_id = stat_name.substr(pos + clusters_pattern.length());
          accepted_connections.push_back(cluster_id);
        }
      }
    }
  }

  ENVOY_LOG(debug, "reverse_tunnel: found {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  return {connected_nodes, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelAcceptorExtension::getCrossWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Iterate through all gauges and filter for cross-worker stats only.
  // Cross-worker stats have the pattern "<stat_prefix>.nodes.<node_id>" or
  // "<stat_prefix>.clusters.<cluster_id>" (no dispatcher name in the middle).
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, this](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "reverse_tunnel: gauge_name: {} gauge_value: {}", gauge_name, gauge->value());
    std::string nodes_pattern = stat_prefix_ + ".nodes.";
    std::string clusters_pattern = stat_prefix_ + ".clusters.";
    if (gauge_name.find(stat_prefix_ + ".") != std::string::npos &&
        (gauge_name.find(nodes_pattern) != std::string::npos ||
         gauge_name.find(clusters_pattern) != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug,
            "reverse_tunnel: collected {} stats for reverse connections across all "
            "worker threads",
            stats_map.size());

  return stats_map;
}

void ReverseTunnelAcceptorExtension::updateConnectionStats(const std::string& node_id,
                                                           const std::string& cluster_id,
                                                           bool increment,
                                                           bool tenant_isolation_enabled) {
  // Update per-worker aggregate metrics.
  updatePerWorkerAggregateMetrics(node_id, cluster_id, increment);

  // Check if reverse tunnel detailed stats are enabled via configuration flag.
  // If detailed stats disabled, don't collect any stats and return early.
  // Stats collection can consume significant memory when the number of nodes/clusters is high.
  if (!enable_detailed_stats_) {
    return;
  }

  // Obtain the stats store.
  auto& stats_store = context_.scope();

  // Log a warning on first activation.
  if (!reverse_tunnel_detailed_stats_warning_logged) {
    ENVOY_LOG(warn, "REVERSE TUNNEL: Detailed per-node/cluster stats are enabled. "
                    "This may consume significant memory with high node counts. "
                    "Monitor memory usage and disable if experiencing issues.");
    reverse_tunnel_detailed_stats_warning_logged = true;
  }

  ReverseConnectionUtility::TenantScopedIdentifierView scoped_node{};
  ReverseConnectionUtility::TenantScopedIdentifierView scoped_cluster{};
  if (tenant_isolation_enabled) {
    scoped_node = ReverseConnectionUtility::splitTenantScopedIdentifier(node_id);
    scoped_cluster = ReverseConnectionUtility::splitTenantScopedIdentifier(cluster_id);
  }

  const auto adjust_gauge = [&](const std::string& stat_name, bool is_increment,
                                Stats::Gauge::ImportMode import_mode) {
    if (stat_name.empty()) {
      return;
    }
    Stats::StatNameManagedStorage stat_name_storage(stat_name, stats_store.symbolTable());
    auto& gauge = stats_store.gaugeFromStatName(stat_name_storage.statName(), import_mode);
    if (is_increment) {
      gauge.inc();
      ENVOY_LOG(trace, "reverse_tunnel: incremented stat {} to {}", stat_name, gauge.value());
    } else if (gauge.value() > 0) {
      gauge.dec();
      ENVOY_LOG(trace, "reverse_tunnel: decremented stat {} to {}", stat_name, gauge.value());
    }
  };

  const absl::string_view base_node_identifier =
      (tenant_isolation_enabled && scoped_node.hasTenant()) ? scoped_node.identifier : node_id;
  const absl::string_view base_cluster_identifier =
      (tenant_isolation_enabled && scoped_cluster.hasTenant()) ? scoped_cluster.identifier
                                                               : cluster_id;

  if (!base_node_identifier.empty()) {
    const std::string node_stat_name =
        fmt::format("{}.nodes.{}", stat_prefix_, base_node_identifier);
    adjust_gauge(node_stat_name, increment, Stats::Gauge::ImportMode::HiddenAccumulate);
    if (tenant_isolation_enabled && scoped_node.hasTenant()) {
      const std::string tenant_node_stat_name = fmt::format(
          "{}.tenants.{}.nodes.{}", stat_prefix_, scoped_node.tenant, scoped_node.identifier);
      adjust_gauge(tenant_node_stat_name, increment, Stats::Gauge::ImportMode::HiddenAccumulate);
    }
  }

  if (!base_cluster_identifier.empty()) {
    const std::string cluster_stat_name =
        fmt::format("{}.clusters.{}", stat_prefix_, base_cluster_identifier);
    adjust_gauge(cluster_stat_name, increment, Stats::Gauge::ImportMode::HiddenAccumulate);
    if (tenant_isolation_enabled && scoped_cluster.hasTenant()) {
      const std::string tenant_cluster_stat_name =
          fmt::format("{}.tenants.{}.clusters.{}", stat_prefix_, scoped_cluster.tenant,
                      scoped_cluster.identifier);
      adjust_gauge(tenant_cluster_stat_name, increment, Stats::Gauge::ImportMode::HiddenAccumulate);
    }
  }

  // Also update per-worker stats for debugging.
  updatePerWorkerConnectionStats(node_id, cluster_id, increment, tenant_isolation_enabled);
}

void ReverseTunnelAcceptorExtension::updatePerWorkerAggregateMetrics(const std::string& node_id,
                                                                     const std::string& cluster_id,
                                                                     bool increment) {
  // Get TLS for current worker.
  auto* local_registry = getLocalRegistry();
  if (local_registry == nullptr) {
    return;
  }

  auto& cluster_counts = local_registry->cluster_connection_counts_;
  auto& node_counts = local_registry->node_connection_counts_;

  // Update counts.
  if (increment) {
    if (!cluster_id.empty()) {
      cluster_counts[cluster_id]++;
    }
    if (!node_id.empty()) {
      node_counts[node_id]++;
    }
  } else {
    if (!cluster_id.empty()) {
      auto it = cluster_counts.find(cluster_id);
      if (it != cluster_counts.end() && it->second > 0) {
        it->second--;
        if (it->second == 0) {
          cluster_counts.erase(it);
        }
      }
    }
    if (!node_id.empty()) {
      auto it = node_counts.find(node_id);
      if (it != node_counts.end() && it->second > 0) {
        it->second--;
        if (it->second == 0) {
          node_counts.erase(it);
        }
      }
    }
  }

  // Update gauges with current map sizes.
  if (local_registry->total_clusters_gauge_ != nullptr) {
    local_registry->total_clusters_gauge_->set(cluster_counts.size());
  }
  if (local_registry->total_nodes_gauge_ != nullptr) {
    local_registry->total_nodes_gauge_->set(node_counts.size());
  }
}

void ReverseTunnelAcceptorExtension::updatePerWorkerConnectionStats(const std::string& node_id,
                                                                    const std::string& cluster_id,
                                                                    bool increment,
                                                                    bool tenant_isolation_enabled) {
  auto& stats_store = context_.scope();

  // Get dispatcher name from the thread local dispatcher.
  std::string dispatcher_name;
  auto* local_registry = getLocalRegistry();
  if (local_registry == nullptr) {
    ENVOY_LOG(error, "reverse_tunnel: No local registry found");
    return;
  }

  // Dispatcher name is of the form "worker_x" where x is the worker index.
  dispatcher_name = local_registry->dispatcher().name();
  ENVOY_LOG(trace, "reverse_tunnel: Updating stats for worker {}", dispatcher_name);

  ReverseConnectionUtility::TenantScopedIdentifierView scoped_node{};
  ReverseConnectionUtility::TenantScopedIdentifierView scoped_cluster{};
  if (tenant_isolation_enabled) {
    scoped_node = ReverseConnectionUtility::splitTenantScopedIdentifier(node_id);
    scoped_cluster = ReverseConnectionUtility::splitTenantScopedIdentifier(cluster_id);
  }

  const absl::string_view worker_node_identifier =
      (tenant_isolation_enabled && scoped_node.hasTenant()) ? scoped_node.identifier : node_id;
  const absl::string_view worker_cluster_identifier =
      (tenant_isolation_enabled && scoped_cluster.hasTenant()) ? scoped_cluster.identifier
                                                               : cluster_id;

  // Create/update per-worker node connection stat.
  if (!worker_node_identifier.empty()) {
    std::string worker_node_stat_name =
        fmt::format("{}.{}.node.{}", stat_prefix_, dispatcher_name, worker_node_identifier);
    Stats::StatNameManagedStorage worker_node_stat_name_storage(worker_node_stat_name,
                                                                stats_store.symbolTable());
    auto& worker_node_gauge = stats_store.gaugeFromStatName(
        worker_node_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_node_gauge.inc();
      ENVOY_LOG(trace, "reverse_tunnel: incremented worker node stat {} to {}",
                worker_node_stat_name, worker_node_gauge.value());
    } else {
      // Guardrail: only decrement if the gauge value is greater than 0
      if (worker_node_gauge.value() > 0) {
        worker_node_gauge.dec();
        ENVOY_LOG(trace, "reverse_tunnel: decremented worker node stat {} to {}",
                  worker_node_stat_name, worker_node_gauge.value());
      } else {
        ENVOY_LOG(trace,
                  "reverse_tunnel: skipping decrement for worker node stat {} "
                  "(already at 0)",
                  worker_node_stat_name);
      }
    }
  }

  // Create/update per-worker cluster connection stat.
  if (!worker_cluster_identifier.empty()) {
    std::string worker_cluster_stat_name =
        fmt::format("{}.{}.cluster.{}", stat_prefix_, dispatcher_name, worker_cluster_identifier);
    Stats::StatNameManagedStorage worker_cluster_stat_name_storage(worker_cluster_stat_name,
                                                                   stats_store.symbolTable());
    auto& worker_cluster_gauge = stats_store.gaugeFromStatName(
        worker_cluster_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_cluster_gauge.inc();
      ENVOY_LOG(trace, "reverse_tunnel: incremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    } else {
      // Guardrail: only decrement if the gauge value is greater than 0
      if (worker_cluster_gauge.value() > 0) {
        worker_cluster_gauge.dec();
        ENVOY_LOG(trace, "reverse_tunnel: decremented worker cluster stat {} to {}",
                  worker_cluster_stat_name, worker_cluster_gauge.value());
      } else {
        ENVOY_LOG(trace,
                  "reverse_tunnel: skipping decrement for worker cluster stat {} "
                  "(already at 0)",
                  worker_cluster_stat_name);
      }
    }
  }
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelAcceptorExtension::getPerWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Get the current dispatcher name.
  std::string dispatcher_name = "main_thread"; // Default for main thread.
  auto* local_registry = getLocalRegistry();
  if (local_registry) {
    // Dispatcher name is of the form "worker_x" where x is the worker index.
    dispatcher_name = local_registry->dispatcher().name();
  }

  // Iterate through all gauges and filter for the current dispatcher.
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, &dispatcher_name, this](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "reverse_tunnel: gauge_name: {} gauge_value: {}", gauge_name, gauge->value());
    if (gauge_name.find(stat_prefix_ + ".") != std::string::npos &&
        gauge_name.find(dispatcher_name + ".") != std::string::npos &&
        (gauge_name.find(".node.") != std::string::npos ||
         gauge_name.find(".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug, "reverse_tunnel: collected {} stats for dispatcher '{}'", stats_map.size(),
            dispatcher_name);

  return stats_map;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
