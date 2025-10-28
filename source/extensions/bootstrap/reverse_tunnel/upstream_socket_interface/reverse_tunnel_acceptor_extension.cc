#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Static warning flag for reverse tunnel detailed stats activation.
static bool reverse_tunnel_detailed_stats_warning_logged = false;

// UpstreamSocketThreadLocal implementation
UpstreamSocketThreadLocal::UpstreamSocketThreadLocal(Event::Dispatcher& dispatcher,
                                                     ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher),
      socket_manager_(std::make_unique<UpstreamSocketManager>(dispatcher, extension)) {}

// ReverseTunnelAcceptorExtension implementation
void ReverseTunnelAcceptorExtension::onServerInitialized() {
  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension::onServerInitialized: creating thread-local slot.");

  // Set the extension reference in the socket interface.
  if (socket_interface_) {
    socket_interface_->extension_ = this;
  }

  // Create thread-local slot for the dispatcher and socket manager.
  tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  // Set up the thread-local dispatcher and socket manager.
  tls_slot_->set([this](Event::Dispatcher& dispatcher) {
    auto tls = std::make_shared<UpstreamSocketThreadLocal>(dispatcher, this);
    // Propagate configured miss threshold into the socket manager.
    if (tls->socketManager()) {
      tls->socketManager()->setMissThreshold(ping_failure_threshold_);
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
                                                           bool increment) {
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

  // Create/update node connection stat.
  if (!node_id.empty()) {
    std::string node_stat_name = fmt::format("{}.nodes.{}", stat_prefix_, node_id);
    Stats::StatNameManagedStorage node_stat_name_storage(node_stat_name, stats_store.symbolTable());
    auto& node_gauge = stats_store.gaugeFromStatName(node_stat_name_storage.statName(),
                                                     Stats::Gauge::ImportMode::HiddenAccumulate);
    if (increment) {
      node_gauge.inc();
      ENVOY_LOG(trace, "reverse_tunnel: incremented node stat {} to {}", node_stat_name,
                node_gauge.value());
    } else {
      if (node_gauge.value() > 0) {
        node_gauge.dec();
        ENVOY_LOG(trace, "reverse_tunnel: decremented node stat {} to {}", node_stat_name,
                  node_gauge.value());
      }
    }
  }

  // Create/update cluster connection stat.
  if (!cluster_id.empty()) {
    std::string cluster_stat_name = fmt::format("{}.clusters.{}", stat_prefix_, cluster_id);
    Stats::StatNameManagedStorage cluster_stat_name_storage(cluster_stat_name,
                                                            stats_store.symbolTable());
    auto& cluster_gauge = stats_store.gaugeFromStatName(cluster_stat_name_storage.statName(),
                                                        Stats::Gauge::ImportMode::HiddenAccumulate);
    if (increment) {
      cluster_gauge.inc();
      ENVOY_LOG(trace, "reverse_tunnel: incremented cluster stat {} to {}", cluster_stat_name,
                cluster_gauge.value());
    } else {
      if (cluster_gauge.value() > 0) {
        cluster_gauge.dec();
        ENVOY_LOG(trace, "reverse_tunnel: decremented cluster stat {} to {}", cluster_stat_name,
                  cluster_gauge.value());
      }
    }
  }

  // Also update per-worker stats for debugging.
  updatePerWorkerConnectionStats(node_id, cluster_id, increment);
}

void ReverseTunnelAcceptorExtension::updatePerWorkerConnectionStats(const std::string& node_id,
                                                                    const std::string& cluster_id,
                                                                    bool increment) {
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

  // Create/update per-worker node connection stat.
  if (!node_id.empty()) {
    std::string worker_node_stat_name =
        fmt::format("{}.{}.node.{}", stat_prefix_, dispatcher_name, node_id);
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
  if (!cluster_id.empty()) {
    std::string worker_cluster_stat_name =
        fmt::format("{}.{}.cluster.{}", stat_prefix_, dispatcher_name, cluster_id);
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
