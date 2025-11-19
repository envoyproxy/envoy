#include "source/extensions/filters/network/redis_proxy/cluster_response_handler.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Implementation of ClusterResponseHandlerFactory
std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createHandler(const std::string& command_name,
                                             const std::string& subcommand, uint32_t shard_count) {
  ClusterScopeResponseHandlerType handler_type = getResponseHandlerType(command_name, subcommand);

  switch (handler_type) {
  case ClusterScopeResponseHandlerType::allresponses_mustbe_same:
    return std::make_unique<AllshardSameResponseHandler>(shard_count);
  case ClusterScopeResponseHandlerType::aggregate_all_responses:
    // Create specific aggregate handler based on command and subcommand
    return createAggregateHandler(command_name, subcommand, shard_count);
  default:
    return nullptr; // No handler for this command
  }
}

std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createAggregateHandler(const std::string& command_name,
                                                      const std::string& subcommand,
                                                      uint32_t shard_count) {

  // SLOWLOG commands
  if (command_name == "slowlog") {
    if (subcommand == "len") {
      return std::make_unique<IntegerSumAggregateResponseHandler>(shard_count);
    } else if (subcommand == "get") {
      return std::make_unique<ArrayMergeAggregateResponseHandler>(shard_count);
    }
  }

  // CONFIG commands
  if (command_name == "config" && subcommand == "get") {
    return std::make_unique<ArrayMergeAggregateResponseHandler>(shard_count);
  }

  // INFO command
  if (command_name == "info") {
    return std::make_unique<InfoCmdAggregateResponseHandler>(shard_count, subcommand);
  }

  // This should never be reached - all commands mapped to aggregate_all_responses
  // should be handled above
  ASSERT(false, fmt::format("Unhandled aggregate command: {}:{}", command_name, subcommand));
  return nullptr; // Unreachable, but needed for compilation
}

std::unique_ptr<BaseClusterScopeResponseHandler>
ClusterResponseHandlerFactory::createFromRequest(const Common::Redis::RespValue& request,
                                                 uint32_t shard_count) {
  if (request.type() != Common::Redis::RespType::Array || request.asArray().empty()) {
    return nullptr;
  }

  const std::string command_name = absl::AsciiStrToLower(request.asArray()[0].asString());
  std::string subcommand = "";

  if (request.asArray().size() > 1) {
    subcommand = absl::AsciiStrToLower(request.asArray()[1].asString());
  }

  return createHandler(command_name, subcommand, shard_count);
}

ClusterScopeResponseHandlerType
ClusterResponseHandlerFactory::getResponseHandlerType(const std::string& command_name,
                                                      const std::string& subcommand) {
  // Based on ClusterScopeCommands: script, flushall, flushdb, slowlog, config, unwatch
  // Note: randomkey and cluster are now handled by RandomShardRequest
  static const absl::flat_hash_map<std::string, ClusterScopeResponseHandlerType>
      command_to_handler_map = {
          // All shards must return same response
          {"script", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"flushall", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"flushdb", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:set", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:rewrite", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"config:resetstat", ClusterScopeResponseHandlerType::allresponses_mustbe_same},
          {"slowlog:reset", ClusterScopeResponseHandlerType::allresponses_mustbe_same},

          // Aggregate responses
          {"config:get", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"slowlog:get", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"slowlog:len", ClusterScopeResponseHandlerType::aggregate_all_responses},
          {"info", ClusterScopeResponseHandlerType::aggregate_all_responses},
      };

  // First check with subcommand to see if there is a map entry
  if (!subcommand.empty()) {
    auto it = command_to_handler_map.find(command_name + ":" + subcommand);
    if (it != command_to_handler_map.end()) {
      return it->second;
    }
  }

  // Fallback to command only search for getting handler type
  auto it = command_to_handler_map.find(command_name);
  if (it != command_to_handler_map.end()) {
    return it->second;
  }
  // Default fallback - no handler found
  return ClusterScopeResponseHandlerType::response_handler_none;
}

// Implementation of BaseClusterScopeResponseHandler - Common functionality for all handlers
void BaseClusterScopeResponseHandler::handleResponse(Common::Redis::RespValuePtr&& value,
                                                     uint32_t shard_index,
                                                     ClusterScopeCmdRequest& request) {
  ENVOY_LOG(debug, "BaseClusterScopeResponseHandler: response received for shard index: '{}'",
            shard_index);

  // Store the response and update counters
  storeResponse(std::move(value), shard_index, request);

  // Early return if not all responses received yet
  ASSERT(num_pending_responses_ > 0);
  if (--num_pending_responses_ > 0) {
    return;
  }

  // Process all responses once we have them all - specific logic per handler type
  processAllResponses(request);
}

void BaseClusterScopeResponseHandler::storeResponse(Common::Redis::RespValuePtr&& value,
                                                    uint32_t shard_index,
                                                    ClusterScopeCmdRequest& request) {
  // Clean up the request handle - for ClusterScopeCmdRequest, shard_index == array index
  request.clearPendingHandle(shard_index);

  // Resize vector if needed to accommodate the shard_index
  if (shard_index >= pending_responses_.size()) {
    pending_responses_.resize(shard_index + 1);
  }

  // Track errors using handler's own state
  if (value && value->type() == Common::Redis::RespType::Error) {
    error_count_++;
  }

  // Store the response
  pending_responses_[shard_index] = std::move(value);
}

void BaseClusterScopeResponseHandler::handleErrorResponses(ClusterScopeCmdRequest& request) {
  request.updateRequestStats(false); // Update stats first for any error case

  // Find and return the first error response
  for (auto& resp : pending_responses_) {
    if (resp && resp->type() == Common::Redis::RespType::Error) {
      ENVOY_LOG(debug, "Error response received: '{}'", resp->toString());
      request.sendResponse(std::move(resp));
      return;
    }
  }
}

void BaseClusterScopeResponseHandler::sendErrorResponse(ClusterScopeCmdRequest& request,
                                                        const std::string& error_message) {
  ENVOY_LOG(error, "ClusterScopeResponseHandler error: {}", error_message);
  request.updateRequestStats(false);
  request.sendResponse(Common::Redis::Utility::makeError(error_message));
}

void BaseClusterScopeResponseHandler::sendSuccessResponse(ClusterScopeCmdRequest& request,
                                                          Common::Redis::RespValuePtr&& response) {
  ENVOY_LOG(debug, "Success response: {}", response->toString());
  request.updateRequestStats(true);
  request.sendResponse(std::move(response));
}

// Implementation of AllshardSameResponseHandler - Specific logic for same-response validation
void AllshardSameResponseHandler::processAllResponses(ClusterScopeCmdRequest& request) {

  ASSERT(!pending_responses_
              .empty()); // Empty responses should never happen for cluster scope commands

  // Handle error responses first
  if (error_count_ > 0) {
    handleErrorResponses(request);
    return;
  }

  // Validate all responses are the same
  if (!areAllResponsesSame()) {
    // Check if we have at least one response for logging
    if (!pending_responses_.empty() && pending_responses_[0]) {
      ENVOY_LOG(debug, "All responses not same: '{}'", pending_responses_[0]->toString());
    } else {
      ENVOY_LOG(debug, "All responses not same: responses are null or empty");
    }
    sendErrorResponse(request, "all responses not same");
    return;
  }

  // Success case - all responses are the same
  sendSuccessResponse(request, std::move(pending_responses_[0]));
}

bool AllshardSameResponseHandler::areAllResponsesSame() const {
  ASSERT(!pending_responses_.empty()); // Empty responses should never happen

  const Common::Redis::RespValue* first_response = pending_responses_.front().get();
  for (const auto& response : pending_responses_) {
    if (!response || !first_response || *(response.get()) != *first_response) {
      return false;
    }
  }
  return true;
}

// Implementation of BaseAggregateResponseHandler - Common aggregation pattern
void BaseAggregateResponseHandler::processAllResponses(ClusterScopeCmdRequest& request) {
  // Handle error responses first
  if (error_count_ > 0) {
    handleErrorResponses(request);
    return;
  }

  // Validate that we have responses - this should always be true for cluster scope commands
  ASSERT(!pending_responses_.empty());

  // Process aggregated responses - specific logic per handler type
  processAggregatedResponses(request);
}

// Implementation of IntegerSumAggregateResponseHandler
void IntegerSumAggregateResponseHandler::processAggregatedResponses(
    ClusterScopeCmdRequest& request) {
  int64_t sum = 0;
  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::Integer) {
      sendErrorResponse(request, "non-integer response received from shard");
      return;
    }

    TRY_NEEDS_AUDIT {
      int64_t integerValue = resp->asInteger();
      if (integerValue < 0) {
        ENVOY_LOG(error, "Error: Negative integer value: {}", integerValue);
        sendErrorResponse(request, "negative value received from upstream");
        return;
      }
      sum += integerValue;
    }
    END_TRY
    CATCH(const std::exception& e, {
      ENVOY_LOG(error, "Error converting integer: {}", e.what());
      sendErrorResponse(request, "invalid integer response from upstream");
      return;
    });
  }

  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::Integer);
  response->asInteger() = sum;
  sendSuccessResponse(request, std::move(response));
}

// Implementation of ArrayMergeAggregateResponseHandler
void ArrayMergeAggregateResponseHandler::processAggregatedResponses(
    ClusterScopeCmdRequest& request) {
  Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::Array);

  for (const auto& resp : pending_responses_) {
    if (!resp) {
      sendErrorResponse(request, "null response received from shard");
      return;
    }

    if (resp->type() != Common::Redis::RespType::Array) {
      sendErrorResponse(request, "non-array response received from shard");
      return;
    }

    TRY_NEEDS_AUDIT {
      for (auto& elem : resp->asArray()) {
        response->asArray().emplace_back(std::move(elem));
      }
    }
    END_TRY
    CATCH(const std::exception& e, {
      ENVOY_LOG(error, "Error merging array: {}", e.what());
      sendErrorResponse(request, "error merging array responses");
      return;
    });
  }

  sendSuccessResponse(request, std::move(response));
}

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

    TRY_NEEDS_AUDIT {
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
    END_TRY
    CATCH(const std::exception& e, {
      ENVOY_LOG(error, "Error parsing INFO response: {}", e.what());
      sendErrorResponse(request, "error parsing INFO response from shard");
      return;
    });
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
    snprintf(buffer, sizeof(buffer), "%lluB", bytes);
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
    snprintf(buffer, sizeof(buffer), "%lluB", bytes);
  }

  return std::string(buffer);
}

bool InfoCmdAggregateResponseHandler::shouldIncludeSection(absl::string_view section) const {
  if (info_section_.empty() || info_section_ == "") {
    return true;
  }
  return absl::EqualsIgnoreCase(section, info_section_);
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
