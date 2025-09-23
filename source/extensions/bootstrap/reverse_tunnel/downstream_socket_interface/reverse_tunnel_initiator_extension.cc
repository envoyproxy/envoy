#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// ReverseTunnelInitiatorExtension implementation
void ReverseTunnelInitiatorExtension::onServerInitialized() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension::onServerInitialized");
}

void ReverseTunnelInitiatorExtension::onWorkerThreadInitialized() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: creating thread local slot");

  // Create thread local slot on worker thread initialization.
  tls_slot_ =
      ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(context_.threadLocal());

  // Set up the thread local dispatcher for each worker thread.
  tls_slot_->set([this](Event::Dispatcher& dispatcher) {
    return std::make_shared<DownstreamSocketThreadLocal>(dispatcher, context_.scope());
  });

  ENVOY_LOG(
      debug,
      "ReverseTunnelInitiatorExtension: thread local slot created successfully in worker thread");
}

DownstreamSocketThreadLocal* ReverseTunnelInitiatorExtension::getLocalRegistry() const {
  if (!tls_slot_) {
    ENVOY_LOG(error, "ReverseTunnelInitiatorExtension: no thread local slot");
    return nullptr;
  }

  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }

  return nullptr;
}

void ReverseTunnelInitiatorExtension::updateConnectionStats(const std::string& host_address,
                                                            const std::string& cluster_id,
                                                            const std::string& state_suffix,
                                                            bool increment) {
  // Register stats with Envoy's system for automatic cross-thread aggregation.
  auto& stats_store = context_.scope();

  // Create/update host connection stat with state suffix
  if (!host_address.empty() && !state_suffix.empty()) {
    std::string host_stat_name =
        fmt::format("{}.host.{}.{}", stat_prefix_, host_address, state_suffix);
    Stats::StatNameManagedStorage host_stat_name_storage(host_stat_name, stats_store.symbolTable());
    auto& host_gauge = stats_store.gaugeFromStatName(host_stat_name_storage.statName(),
                                                     Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      host_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented host stat {} to {}",
                host_stat_name, host_gauge.value());
    } else {
      host_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented host stat {} to {}",
                host_stat_name, host_gauge.value());
    }
  }

  // Create/update cluster connection stat with state suffix.
  if (!cluster_id.empty() && !state_suffix.empty()) {
    std::string cluster_stat_name =
        fmt::format("{}.cluster.{}.{}", stat_prefix_, cluster_id, state_suffix);
    Stats::StatNameManagedStorage cluster_stat_name_storage(cluster_stat_name,
                                                            stats_store.symbolTable());
    auto& cluster_gauge = stats_store.gaugeFromStatName(cluster_stat_name_storage.statName(),
                                                        Stats::Gauge::ImportMode::Accumulate);
    if (increment) {
      cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    } else {
      cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented cluster stat {} to {}",
                cluster_stat_name, cluster_gauge.value());
    }
  }

  // Also update per-worker stats for debugging.
  updatePerWorkerConnectionStats(host_address, cluster_id, state_suffix, increment);
}

void ReverseTunnelInitiatorExtension::updatePerWorkerConnectionStats(
    const std::string& host_address, const std::string& cluster_id, const std::string& state_suffix,
    bool increment) {
  auto& stats_store = context_.scope();

  // Get dispatcher name from the thread local dispatcher.
  std::string dispatcher_name;
  auto* local_registry = getLocalRegistry();
  if (local_registry == nullptr) {
    ENVOY_LOG(error, "ReverseTunnelInitiatorExtension: No local registry found");
    return;
  }
  // Dispatcher name is of the form "worker_x" where x is the worker index
  dispatcher_name = local_registry->dispatcher().name();
  ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: Updating stats for worker {}",
            dispatcher_name);

  // Create/update per-worker host connection stat.
  if (!host_address.empty() && !state_suffix.empty()) {
    std::string worker_host_stat_name =
        fmt::format("{}.{}.host.{}.{}", stat_prefix_, dispatcher_name, host_address, state_suffix);
    Stats::StatNameManagedStorage worker_host_stat_name_storage(worker_host_stat_name,
                                                                stats_store.symbolTable());
    auto& worker_host_gauge = stats_store.gaugeFromStatName(
        worker_host_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_host_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented worker host stat {} to {}",
                worker_host_stat_name, worker_host_gauge.value());
    } else {
      worker_host_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented worker host stat {} to {}",
                worker_host_stat_name, worker_host_gauge.value());
    }
  }

  // Create/update per-worker cluster connection stat.
  if (!cluster_id.empty() && !state_suffix.empty()) {
    std::string worker_cluster_stat_name =
        fmt::format("{}.{}.cluster.{}.{}", stat_prefix_, dispatcher_name, cluster_id, state_suffix);
    Stats::StatNameManagedStorage worker_cluster_stat_name_storage(worker_cluster_stat_name,
                                                                   stats_store.symbolTable());
    auto& worker_cluster_gauge = stats_store.gaugeFromStatName(
        worker_cluster_stat_name_storage.statName(), Stats::Gauge::ImportMode::NeverImport);
    if (increment) {
      worker_cluster_gauge.inc();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: incremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    } else {
      worker_cluster_gauge.dec();
      ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: decremented worker cluster stat {} to {}",
                worker_cluster_stat_name, worker_cluster_gauge.value());
    }
  }
}

absl::flat_hash_map<std::string, uint64_t>
ReverseTunnelInitiatorExtension::getCrossWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Iterate through all gauges and filter for cross-worker stats only.
  // Cross-worker stats have the pattern "<stat_prefix>.host.<host_address>.<state_suffix>" or
  // "<stat_prefix>.cluster.<cluster_id>.<state_suffix>" (no dispatcher name in the middle).
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, this](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find(stat_prefix_ + ".") != std::string::npos &&
        (gauge_name.find(stat_prefix_ + ".host.") != std::string::npos ||
         gauge_name.find(stat_prefix_ + ".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(
      debug,
      "ReverseTunnelInitiatorExtension: collected {} stats for reverse connections across all "
      "worker threads",
      stats_map.size());

  return stats_map;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ReverseTunnelInitiatorExtension::getConnectionStatsSync(
    std::chrono::milliseconds /* timeout_ms */) {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: obtaining reverse connection stats");

  // Get all gauges with the reverse_connections prefix.
  auto connection_stats = getCrossWorkerStatMap();

  std::vector<std::string> connected_hosts;
  std::vector<std::string> accepted_connections;

  // Process the stats to extract connection information
  // For initiator, stats format is: <stat_prefix>.host.<host>.<state_suffix> or
  // <stat_prefix>.cluster.<cluster>.<state_suffix> We only want hosts/clusters with
  // "connected" state
  for (const auto& [stat_name, count] : connection_stats) {
    if (count > 0) {
      // Parse stat name to extract host/cluster information with state suffix.
      std::string host_pattern = stat_prefix_ + ".host.";
      std::string cluster_pattern = stat_prefix_ + ".cluster.";

      if (stat_name.find(host_pattern) != std::string::npos &&
          stat_name.find(".connected") != std::string::npos) {
        // Find the position after "<stat_prefix>.host." and before ".connected".
        size_t start_pos = stat_name.find(host_pattern) + host_pattern.length();
        size_t end_pos = stat_name.find(".connected");
        if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos > start_pos) {
          std::string host_address = stat_name.substr(start_pos, end_pos - start_pos);
          connected_hosts.push_back(host_address);
        }
      } else if (stat_name.find(cluster_pattern) != std::string::npos &&
                 stat_name.find(".connected") != std::string::npos) {
        // Find the position after "<stat_prefix>.cluster." and before ".connected".
        size_t start_pos = stat_name.find(cluster_pattern) + cluster_pattern.length();
        size_t end_pos = stat_name.find(".connected");
        if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos > start_pos) {
          std::string cluster_id = stat_name.substr(start_pos, end_pos - start_pos);
          accepted_connections.push_back(cluster_id);
        }
      }
    }
  }

  ENVOY_LOG(debug,
            "ReverseTunnelInitiatorExtension: found {} connected hosts, {} accepted connections",
            connected_hosts.size(), accepted_connections.size());

  return {connected_hosts, accepted_connections};
}

absl::flat_hash_map<std::string, uint64_t> ReverseTunnelInitiatorExtension::getPerWorkerStatMap() {
  absl::flat_hash_map<std::string, uint64_t> stats_map;
  auto& stats_store = context_.scope();

  // Get the current dispatcher name
  std::string dispatcher_name = "main_thread"; // Default for main thread
  auto* local_registry = getLocalRegistry();
  if (local_registry) {
    // Dispatcher name is of the form "worker_x" where x is the worker index.
    dispatcher_name = local_registry->dispatcher().name();
  }
  ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: Getting per worker stats map for {}",
            dispatcher_name);

  // Iterate through all gauges and filter for the current dispatcher.
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&stats_map, &dispatcher_name, this](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    ENVOY_LOG(trace, "ReverseTunnelInitiatorExtension: gauge_name: {} gauge_value: {}", gauge_name,
              gauge->value());
    if (gauge_name.find(stat_prefix_ + ".") != std::string::npos &&
        gauge_name.find(dispatcher_name + ".") != std::string::npos &&
        (gauge_name.find(".host.") != std::string::npos ||
         gauge_name.find(".cluster.") != std::string::npos) &&
        gauge->used()) {
      stats_map[gauge_name] = gauge->value();
    }
    return true;
  };
  stats_store.iterate(gauge_callback);

  ENVOY_LOG(debug, "ReverseTunnelInitiatorExtension: collected {} stats for dispatcher '{}'",
            stats_map.size(), dispatcher_name);

  return stats_map;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
