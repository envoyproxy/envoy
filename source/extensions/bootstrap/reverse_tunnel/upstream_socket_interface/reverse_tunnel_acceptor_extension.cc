#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// UpstreamSocketThreadLocal implementation
UpstreamSocketThreadLocal::UpstreamSocketThreadLocal(Event::Dispatcher& dispatcher,
                                                     ReverseTunnelAcceptorExtension* extension)
    : dispatcher_(dispatcher),
      socket_manager_(std::make_unique<UpstreamSocketManager>(dispatcher, extension)) {}

// ReverseTunnelAcceptorExtension implementation
void ReverseTunnelAcceptorExtension::onServerInitialized() {
  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension::onServerInitialized - creating thread local slot");

  // Set the extension reference in the socket interface.
  if (socket_interface_) {
    socket_interface_->extension_ = this;
  }

  // Create thread local slot for dispatcher and socket manager.
  tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  // Set up the thread local dispatcher and socket manager.
  tls_slot_->set([this](Event::Dispatcher& dispatcher) {
    return std::make_shared<UpstreamSocketThreadLocal>(dispatcher, this);
  });
}

// Get thread local registry for the current thread
UpstreamSocketThreadLocal* ReverseTunnelAcceptorExtension::getLocalRegistry() const {
  if (!tls_slot_) {
    ENVOY_LOG(error, "ReverseTunnelAcceptorExtension::getLocalRegistry() - no thread local slot");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelAcceptorExtension::getConnectionStatsSync(std::chrono::milliseconds /* timeout_ms */) {

  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension: obtaining reverse connection stats");

  // Get all gauges with the reverse_connections prefix.
  auto connection_stats = getCrossWorkerStatMap();

  std::vector<std::string> connected_nodes;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract node/cluster information.
      // Format: "<scope>.reverse_connections.nodes.<node_id>" or
      // "<scope>.reverse_connections.clusters.<cluster_id>".
      if (stat_name.find("reverse_connections.nodes.") != std::string::npos) {
        // Find the position after "reverse_connections.nodes.".
        size_t pos = stat_name.find("reverse_connections.nodes.");
        if (pos != std::string::npos) {
          std::string node_id = stat_name.substr(pos + strlen("reverse_connections.nodes."));
          connected_nodes.push_back(node_id);
        }
      } else if (stat_name.find("reverse_connections.clusters.") != std::string::npos) {
        // Find the position after "reverse_connections.clusters.".
        size_t pos = stat_name.find("reverse_connections.clusters.");
        if (pos != std::string::npos) {
          std::string cluster_id = stat_name.substr(pos + strlen("reverse_connections.clusters."));
          accepted_connections.push_back(cluster_id);
        }
      }
    }
  }

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension: found {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  return {connected_nodes, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelAcceptorExtension::getCrossWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Iterate through all gauges and filter for cross-worker stats only.
  // Cross-worker stats have the pattern "reverse_connections.nodes.<node_id>" or
  // "reverse_connections.clusters.<cluster_id>" (no dispatcher name in the middle).
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        (gauge_name.find("reverse_connections.nodes.") != std::string::npos ||
         gauge_name.find("reverse_connections.clusters.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug,
            "ReverseTunnelAcceptorExtension: collected {} stats for reverse connections across all "
            "worker threads",
            stats_map.size());

  return stats_map;
}

void ReverseTunnelAcceptorExtension::updateConnectionStats(const std::string& node_id,
                                                           const std::string& cluster_id,
                                                           bool increment) {

  // Register stats with Envoy's system for automatic cross-thread aggregation
  auto& stats_store = context_.scope();

  // Create/update node connection stat
  if (!node_id.empty()) {
    std::string node_stat_name = fmt::format("reverse_connections.nodes.{}", node_id);
    Stats::StatNameManagedStorage node_stat_name_storage(node_stat_name, stats_store.symbolTable());
    auto& node_gauge = stats_store.gaugeFromStatName(node_stat_name_storage.statName(),
                                                     Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      node_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented node stat {} to {}",
                node_stat_name, node_gauge.value());
    } else {
      if (node_gauge.value() > 0) {
        node_gauge.dec();
        ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented node stat {} to {}",
                  node_stat_name, node_gauge.value());
      }
    }
  }

  // Create/update cluster connection stat.
  if (!cluster_id.empty()) {
    std::string cluster_stat_name = fmt::format("reverse_connections.clusters.{}", cluster_id);
    Stats::StatNameManagedStorage cluster_stat_name_storage(cluster_stat_name,
                                                            stats_store.symbolTable());
    auto& cluster_gauge = stats_store.gaugeFromStatName(cluster_stat_name_storage.statName(),
                                                        Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    } else {
      if (cluster_gauge.value() > 0) {
        cluster_gauge.dec();
        ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented cluster stat {} to {}",
                  cluster_stat_name, cluster_gauge.value());
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
    ENVOY_LOG(error, "ReverseTunnelAcceptorExtension: No local registry found");
    return;
  }

  // Dispatcher name is of the form "worker_x" where x is the worker index
  dispatcher_name = local_registry->dispatcher().name();
  ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: Updating stats for worker {}", dispatcher_name);

  // Create/update per-worker node connection stat
  if (!node_id.empty()) {
    std::string worker_node_stat_name =
        fmt::format("reverse_connections.{}.node.{}", dispatcher_name, node_id);
    Stats::StatNameManagedStorage worker_node_stat_name_storage(worker_node_stat_name,
                                                                stats_store.symbolTable());
    auto& worker_node_gauge = stats_store.gaugeFromStatName(
        worker_node_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_node_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented worker node stat {} to {}",
                worker_node_stat_name, worker_node_gauge.value());
    } else {
      // Guardrail: only decrement if the gauge value is greater than 0
      if (worker_node_gauge.value() > 0) {
        worker_node_gauge.dec();
        ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented worker node stat {} to {}",
                  worker_node_stat_name, worker_node_gauge.value());
      } else {
        ENVOY_LOG(trace,
                  "ReverseTunnelAcceptorExtension: skipping decrement for worker node stat {} "
                  "(already at 0)",
                  worker_node_stat_name);
      }
    }
  }

  // Create/update per-worker cluster connection stat
  if (!cluster_id.empty()) {
    std::string worker_cluster_stat_name =
        fmt::format("reverse_connections.{}.cluster.{}", dispatcher_name, cluster_id);
    Stats::StatNameManagedStorage worker_cluster_stat_name_storage(worker_cluster_stat_name,
                                                                   stats_store.symbolTable());
    auto& worker_cluster_gauge = stats_store.gaugeFromStatName(
        worker_cluster_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: incremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    } else {
      // Guardrail: only decrement if the gauge value is greater than 0
      if (worker_cluster_gauge.value() > 0) {
        worker_cluster_gauge.dec();
        ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: decremented worker cluster stat {} to {}",
                  worker_cluster_stat_name, worker_cluster_gauge.value());
      } else {
        ENVOY_LOG(trace,
                  "ReverseTunnelAcceptorExtension: skipping decrement for worker cluster stat {} "
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
      [&stats_map, &dispatcher_name](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelAcceptorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find("reverse_connections.") != std::string::npos &&
        gauge_name.find(dispatcher_name + ".") != std::string::npos &&
        (gauge_name.find(".node.") != std::string::npos ||
         gauge_name.find(".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug, "ReverseTunnelAcceptorExtension: collected {} stats for dispatcher '{}'",
            stats_map.size(), dispatcher_name);

  return stats_map;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
