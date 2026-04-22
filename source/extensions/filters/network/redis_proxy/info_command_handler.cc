#include "source/extensions/filters/network/redis_proxy/info_command_handler.h"

#include <cinttypes>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Implementation of InfoCmdAggregateResponseHandler

void InfoCmdAggregateResponseHandler::initializeMetricTemplate() {
  // Define all metrics with their section, key, and aggregation type
  // This follows the pattern from the reference code for maintainability
  metric_template_ = {
      // Server section
      {"Server", "redis_version", AggregationType::First},
      {"Server", "redis_git_sha1", AggregationType::First},
      {"Server", "redis_git_dirty", AggregationType::First},
      {"Server", "redis_build_id", AggregationType::First},
      {"Server", "redis_mode", AggregationType::Constant, "cluster"},
      {"Server", "os", AggregationType::First},
      {"Server", "arch_bits", AggregationType::First},
      {"Server", "monotonic_clock", AggregationType::First},
      {"Server", "gcc_version", AggregationType::First},
      {"Server", "multiplexing_api", AggregationType::First},
      {"Server", "atomicvar_api", AggregationType::First},
      {"Server", "process_supervised", AggregationType::First},
      {"Server", "run_id", AggregationType::First},
      {"Server", "server_time_usec", AggregationType::First},
      {"Server", "uptime_in_seconds", AggregationType::Max}, // Oldest shard uptime
      {"Server", "uptime_in_days", AggregationType::Max},    // Oldest shard uptime
      {"Server", "hz", AggregationType::First},
      {"Server", "configured_hz", AggregationType::First},
      {"Server", "lru_clock", AggregationType::First},
      {"Server", "executable", AggregationType::First},
      {"Server", "config_file", AggregationType::First},
      {"Server", "io_threads_active", AggregationType::Max},

      // Clients section
      {"Clients", "connected_clients", AggregationType::Sum},
      {"Clients", "cluster_connections", AggregationType::Sum},
      {"Clients", "maxclients", AggregationType::First},
      {"Clients", "client_recent_max_input_buffer",
       AggregationType::Max}, // Gives the largest buffer across shards
      {"Clients", "client_recent_max_output_buffer",
       AggregationType::Max}, // Gives the largest buffer across shards
      {"Clients", "blocked_clients", AggregationType::Sum},
      {"Clients", "tracking_clients", AggregationType::Sum},
      {"Clients", "pubsub_clients", AggregationType::Sum},
      {"Clients", "watching_clients", AggregationType::Sum},
      {"Clients", "clients_in_timeout_table", AggregationType::Sum},
      {"Clients", "total_watched_keys", AggregationType::Sum},
      {"Clients", "total_blocking_keys", AggregationType::Sum},
      {"Clients", "total_blocking_keys_on_nokey", AggregationType::Sum},

      // Memory section
      {"Memory", "used_memory", AggregationType::Sum},
      {"Memory", "used_memory_human", AggregationType::PostProcess, "", nullptr, "used_memory"},
      {"Memory", "used_memory_rss", AggregationType::Sum},
      {"Memory", "used_memory_rss_human", AggregationType::PostProcess, "", nullptr,
       "used_memory_rss"},
      {"Memory", "used_memory_peak", AggregationType::Max}, // Highest peak across all shards
      {"Memory", "used_memory_peak_human", AggregationType::PostProcess, "", nullptr,
       "used_memory_peak"},
      {"Memory", "used_memory_peak_time", AggregationType::First}, // Time of first observed peak
      {"Memory", "used_memory_overhead", AggregationType::Sum},
      {"Memory", "used_memory_startup", AggregationType::Sum},
      {"Memory", "used_memory_dataset", AggregationType::Sum},
      {"Memory", "total_system_memory",
       AggregationType::Sum}, // Total memory across all cluster nodes
      {"Memory", "total_system_memory_human", AggregationType::PostProcess, "", nullptr,
       "total_system_memory"},
      {"Memory", "used_memory_lua", AggregationType::Sum},
      {"Memory", "used_memory_vm_eval", AggregationType::Sum},
      {"Memory", "used_memory_lua_human", AggregationType::PostProcess, "", nullptr,
       "used_memory_lua"},
      {"Memory", "used_memory_scripts_eval", AggregationType::Sum},
      {"Memory", "number_of_cached_scripts", AggregationType::Sum},
      {"Memory", "number_of_functions", AggregationType::Sum},
      {"Memory", "number_of_libraries", AggregationType::Sum},
      {"Memory", "used_memory_vm_functions", AggregationType::Sum},
      {"Memory", "used_memory_vm_total", AggregationType::Sum},
      {"Memory", "used_memory_vm_total_human", AggregationType::PostProcess, "", nullptr,
       "used_memory_vm_total"},
      {"Memory", "used_memory_functions", AggregationType::Sum},
      {"Memory", "used_memory_scripts", AggregationType::Sum},
      {"Memory", "used_memory_scripts_human", AggregationType::PostProcess, "", nullptr,
       "used_memory_scripts"},
      {"Memory", "maxmemory", AggregationType::Sum}, // Total max memory across all shards
      {"Memory", "maxmemory_human", AggregationType::PostProcess, "", nullptr, "maxmemory"},
      {"Memory", "maxmemory_policy", AggregationType::First},
      {"Memory", "mem_fragmentation_bytes", AggregationType::Sum},
      {"Memory", "allocator_frag_bytes", AggregationType::Sum},
      {"Memory", "allocator_rss_bytes", AggregationType::Sum},
      {"Memory", "rss_overhead_bytes", AggregationType::Sum},
      {"Memory", "allocator_allocated", AggregationType::Sum},
      {"Memory", "allocator_active", AggregationType::Sum},
      {"Memory", "allocator_resident", AggregationType::Sum},
      {"Memory", "allocator_muzzy", AggregationType::Sum},
      {"Memory", "mem_not_counted_for_evict", AggregationType::Sum},
      {"Memory", "mem_clients_slaves", AggregationType::Sum},
      {"Memory", "mem_clients_normal", AggregationType::Sum},
      {"Memory", "mem_cluster_links", AggregationType::Sum},
      {"Memory", "mem_cluster_slot_migration_output_buffer", AggregationType::Sum},
      {"Memory", "mem_cluster_slot_migration_input_buffer", AggregationType::Sum},
      {"Memory", "mem_cluster_slot_migration_input_buffer_peak", AggregationType::Max},
      {"Memory", "mem_aof_buffer", AggregationType::Sum},
      {"Memory", "mem_replication_backlog", AggregationType::Sum},
      {"Memory", "mem_total_replication_buffers", AggregationType::Sum},
      {"Memory", "mem_allocator", AggregationType::First},
      {"Memory", "mem_overhead_db_hashtable_rehashing", AggregationType::Sum},
      {"Memory", "active_defrag_running",
       AggregationType::Max}, // Flag: 1 if ANY shard is running active defragmentation
      {"Memory", "lazyfree_pending_objects", AggregationType::Sum},
      {"Memory", "lazyfreed_objects", AggregationType::Sum},

      // Stats section
      {"Stats", "total_connections_received", AggregationType::Sum},
      {"Stats", "total_commands_processed", AggregationType::Sum},
      {"Stats", "instantaneous_ops_per_sec", AggregationType::Sum}, // Aggregate throughput
      {"Stats", "total_net_input_bytes", AggregationType::Sum},
      {"Stats", "total_net_output_bytes", AggregationType::Sum},
      {"Stats", "total_net_repl_input_bytes", AggregationType::Sum},
      {"Stats", "total_net_repl_output_bytes", AggregationType::Sum},
      {"Stats", "instantaneous_input_kbps", AggregationType::Sum},       // Aggregate bandwidth
      {"Stats", "instantaneous_output_kbps", AggregationType::Sum},      // Aggregate bandwidth
      {"Stats", "instantaneous_input_repl_kbps", AggregationType::Sum},  // Aggregate bandwidth
      {"Stats", "instantaneous_output_repl_kbps", AggregationType::Sum}, // Aggregate bandwidth
      {"Stats", "rejected_connections", AggregationType::Sum},
      {"Stats", "sync_full", AggregationType::Sum},
      {"Stats", "sync_partial_ok", AggregationType::Sum},
      {"Stats", "sync_partial_err", AggregationType::Sum},
      {"Stats", "expired_subkeys", AggregationType::Sum},
      {"Stats", "expired_keys", AggregationType::Sum},
      {"Stats", "expired_stale_perc", AggregationType::Max}, // Worst case percentage
      {"Stats", "expired_time_cap_reached_count", AggregationType::Sum},
      {"Stats", "expire_cycle_cpu_milliseconds", AggregationType::Sum},
      {"Stats", "evicted_keys", AggregationType::Sum},
      {"Stats", "evicted_clients", AggregationType::Sum},
      {"Stats", "evicted_scripts", AggregationType::Sum},
      {"Stats", "total_eviction_exceeded_time", AggregationType::Sum},
      {"Stats", "current_eviction_exceeded_time", AggregationType::Max}, // Worst case
      {"Stats", "keyspace_hits", AggregationType::Sum},
      {"Stats", "keyspace_misses", AggregationType::Sum},
      {"Stats", "pubsub_channels", AggregationType::Sum},
      {"Stats", "pubsub_patterns", AggregationType::Sum},
      {"Stats", "pubsubshard_channels", AggregationType::Sum},
      {"Stats", "latest_fork_usec", AggregationType::Max}, // Most recent/longest fork
      {"Stats", "total_forks", AggregationType::Sum},
      {"Stats", "migrate_cached_sockets", AggregationType::Sum},
      {"Stats", "slave_expires_tracked_keys", AggregationType::Sum},
      {"Stats", "active_defrag_hits", AggregationType::Sum},
      {"Stats", "active_defrag_misses", AggregationType::Sum},
      {"Stats", "active_defrag_key_hits", AggregationType::Sum},
      {"Stats", "active_defrag_key_misses", AggregationType::Sum},
      {"Stats", "total_active_defrag_time", AggregationType::Sum},
      {"Stats", "current_active_defrag_time", AggregationType::Max}, // Worst case
      {"Stats", "tracking_total_keys", AggregationType::Sum},
      {"Stats", "tracking_total_items", AggregationType::Sum},
      {"Stats", "tracking_total_prefixes", AggregationType::Sum},
      {"Stats", "unexpected_error_replies", AggregationType::Sum},
      {"Stats", "total_error_replies", AggregationType::Sum},
      {"Stats", "dump_payload_sanitizations", AggregationType::Sum},
      {"Stats", "total_reads_processed", AggregationType::Sum},
      {"Stats", "total_writes_processed", AggregationType::Sum},
      {"Stats", "io_threaded_reads_processed", AggregationType::Sum},
      {"Stats", "io_threaded_writes_processed", AggregationType::Sum},
      {"Stats", "client_query_buffer_limit_disconnections", AggregationType::Sum},
      {"Stats", "client_output_buffer_limit_disconnections", AggregationType::Sum},
      {"Stats", "reply_buffer_shrinks", AggregationType::Sum},
      {"Stats", "reply_buffer_expands", AggregationType::Sum},
      {"Stats", "eventloop_cycles", AggregationType::Sum},
      {"Stats", "eventloop_duration_sum", AggregationType::Sum},
      {"Stats", "eventloop_duration_cmd_sum", AggregationType::Sum},
      {"Stats", "instantaneous_eventloop_cycles_per_sec", AggregationType::Sum}, // Aggregate rate
      {"Stats", "instantaneous_eventloop_duration_usec",
       AggregationType::Max}, // Worst case latency
      {"Stats", "acl_access_denied_auth", AggregationType::Sum},
      {"Stats", "acl_access_denied_cmd", AggregationType::Sum},
      {"Stats", "acl_access_denied_key", AggregationType::Sum},
      {"Stats", "acl_access_denied_channel", AggregationType::Sum},

      // CPU section
      {"CPU", "used_cpu_sys", AggregationType::Sum},
      {"CPU", "used_cpu_user", AggregationType::Sum},
      {"CPU", "used_cpu_sys_children", AggregationType::Sum},
      {"CPU", "used_cpu_user_children", AggregationType::Sum},
      {"CPU", "used_cpu_sys_main_thread", AggregationType::Sum},
      {"CPU", "used_cpu_user_main_thread", AggregationType::Sum},

      // Cluster section
      {"Cluster", "cluster_enabled", AggregationType::Constant, "1"},

      // Keyspace section - requires custom aggregation
      {"Keyspace", "db0", AggregationType::Custom, "",
       &InfoCmdAggregateResponseHandler::aggregateKeyspaceMetric},
  };

  // Build index for fast lookup: key -> index in vector
  for (size_t i = 0; i < metric_template_.size(); ++i) {
    metric_index_[metric_template_[i].key] = i;
  }
}

void InfoCmdAggregateResponseHandler::processAggregatedResponses(ClusterScopeCmdRequest& request) {

  // Parse and aggregate all shard responses
  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::BulkString) {
      sendErrorResponse(request, "non-bulk-string response received from shard");
      return;
    }

    // Parse response line by line
    absl::string_view response_str = resp->asString();
    std::string current_section;

    for (absl::string_view line : absl::StrSplit(response_str, '\n')) {
      line = absl::StripAsciiWhitespace(line);

      if (line.empty()) {
        continue; // Skip empty lines
      }

      // Check if this is a section header
      if (line[0] == '#') {
        // Extract section name: "# Server" -> "Server"
        absl::string_view section_name = absl::StripPrefix(line, "#");
        section_name = absl::StripLeadingAsciiWhitespace(section_name);
        current_section = std::string(section_name);
        continue;
      }

      // Skip metrics from sections we're not interested in
      if (!shouldIncludeSection(current_section)) {
        continue;
      }

      // Parse key:value
      std::pair<absl::string_view, absl::string_view> kv =
          absl::StrSplit(line, absl::MaxSplits(':', 1));

      if (!kv.first.empty()) {
        processMetric(std::string(kv.first), kv.second);
      }
    }
  }

  // Build final response
  std::string final_response = buildFinalInfoResponse();

  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::BulkString);
  response->asString() = std::move(final_response);

  sendSuccessResponse(request, std::move(response));
}

void InfoCmdAggregateResponseHandler::processMetric(const std::string& key,
                                                    absl::string_view value) {
  // Find metric in template
  auto it = metric_index_.find(key);
  if (it == metric_index_.end()) {
    return; // Unknown metric, skip
  }

  MetricConfig& metric = metric_template_[it->second];

  switch (metric.agg_type) {
  case AggregationType::First:
    if (metric.str_value.empty()) {
      metric.str_value = std::string(value);
    }
    break;

  case AggregationType::Sum: {
    int64_t num_value = 0;
    if (absl::SimpleAtoi(value, &num_value)) {
      metric.int_value += num_value;
    }
    break;
  }

  case AggregationType::Max: {
    int64_t num_value = 0;
    if (absl::SimpleAtoi(value, &num_value)) {
      if (num_value > metric.int_value) {
        metric.int_value = num_value;
      }
    }
    break;
  }

  case AggregationType::Custom:
    if (metric.custom_handler) {
      metric.custom_handler(key, value, metric);
    }
    break;

  case AggregationType::PostProcess:
  case AggregationType::Constant:
    break;
  }
}

std::string InfoCmdAggregateResponseHandler::buildFinalInfoResponse() const {
  std::string result;
  result.reserve(8192);
  std::string last_section;

  for (const auto& metric : metric_template_) {
    if (!shouldIncludeSection(metric.section)) {
      continue;
    }

    std::string value_str;
    bool has_value = false;

    switch (metric.agg_type) {
    case AggregationType::PostProcess:
      if (!metric.source_metric_for_human.empty()) {
        auto it = metric_index_.find(metric.source_metric_for_human);
        if (it != metric_index_.end() && metric_template_[it->second].int_value > 0) {
          value_str = bytesToHuman(static_cast<uint64_t>(metric_template_[it->second].int_value));
          has_value = true;
        }
      }
      break;

    case AggregationType::Sum:
    case AggregationType::Max:
      value_str = std::to_string(metric.int_value);
      has_value = true;
      break;

    case AggregationType::First:
    case AggregationType::Constant:
    case AggregationType::Custom:
      if (!metric.str_value.empty()) {
        value_str = metric.str_value;
        has_value = true;
      }
      break;
    }

    if (!has_value) {
      continue;
    }

    if (metric.section != last_section) {
      if (!last_section.empty()) {
        result += "\r\n";
      }
      result += "# " + metric.section + "\r\n";
      last_section = metric.section;
    }

    result += metric.key + ":" + value_str + "\r\n";
  }

  return result;
}

void InfoCmdAggregateResponseHandler::parseKeyspaceStats(absl::string_view value, uint64_t& keys,
                                                         uint64_t& expires, uint64_t& avg_ttl) {
  keys = 0;
  expires = 0;
  avg_ttl = 0;

  // Parse format: keys=95078,expires=403,avg_ttl=31309382249966
  for (absl::string_view kv_pair : absl::StrSplit(value, ',')) {
    std::pair<absl::string_view, absl::string_view> kv =
        absl::StrSplit(kv_pair, absl::MaxSplits('=', 1));

    if (kv.first == "keys") {
      (void)absl::SimpleAtoi(kv.second, &keys);
    } else if (kv.first == "expires") {
      (void)absl::SimpleAtoi(kv.second, &expires);
    } else if (kv.first == "avg_ttl") {
      (void)absl::SimpleAtoi(kv.second, &avg_ttl);
    }
  }
}

void InfoCmdAggregateResponseHandler::aggregateKeyspaceMetric(const std::string&,
                                                              absl::string_view value,
                                                              MetricConfig& metric) {
  uint64_t old_keys = 0, old_expires = 0, old_avg_ttl = 0;
  uint64_t curr_keys = 0, curr_expires = 0, curr_avg_ttl = 0;

  if (!metric.str_value.empty()) {
    parseKeyspaceStats(metric.str_value, old_keys, old_expires, old_avg_ttl);
  }

  parseKeyspaceStats(value, curr_keys, curr_expires, curr_avg_ttl);

  uint64_t total_keys = curr_keys + old_keys;
  uint64_t total_expires = curr_expires + old_expires;
  uint64_t total_avg_ttl = (curr_avg_ttl + old_avg_ttl) / 2;

  metric.str_value = "keys=" + std::to_string(total_keys) +
                     ",expires=" + std::to_string(total_expires) +
                     ",avg_ttl=" + std::to_string(total_avg_ttl);
}

// Helper function: Convert bytes to human-readable format
// Follows Redis bytesToHuman() format exactly
std::string InfoCmdAggregateResponseHandler::bytesToHuman(uint64_t bytes) {
  char buffer[64];

  if (bytes < 1024) {
    snprintf(buffer, sizeof(buffer), "%" PRIu64 "B", bytes);
  } else if (bytes < (1024ULL * 1024)) {
    double d = static_cast<double>(bytes) / 1024.0;
    snprintf(buffer, sizeof(buffer), "%.2fK", d);
  } else if (bytes < (1024ULL * 1024 * 1024)) {
    double d = static_cast<double>(bytes) / (1024.0 * 1024);
    snprintf(buffer, sizeof(buffer), "%.2fM", d);
  } else if (bytes < (1024ULL * 1024 * 1024 * 1024)) {
    double d = static_cast<double>(bytes) / (1024.0 * 1024 * 1024);
    snprintf(buffer, sizeof(buffer), "%.2fG", d);
  } else if (bytes < (1024ULL * 1024 * 1024 * 1024 * 1024)) {
    double d = static_cast<double>(bytes) / (1024.0 * 1024 * 1024 * 1024);
    snprintf(buffer, sizeof(buffer), "%.2fT", d);
  } else if (bytes < (1024ULL * 1024 * 1024 * 1024 * 1024 * 1024)) {
    double d = static_cast<double>(bytes) / (1024.0 * 1024 * 1024 * 1024 * 1024);
    snprintf(buffer, sizeof(buffer), "%.2fP", d);
  } else {
    snprintf(buffer, sizeof(buffer), "%" PRIu64 "B", bytes);
  }

  return std::string(buffer);
}

bool InfoCmdAggregateResponseHandler::shouldIncludeSection(absl::string_view section) const {
  if (info_section_.empty()) {
    return true;
  }
  return absl::EqualsIgnoreCase(section, info_section_);
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
