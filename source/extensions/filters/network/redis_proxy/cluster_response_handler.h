#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Forward declaration to avoid circular dependency
class ClusterScopeCmdRequest;

/**
 * Enum defining the different response handler types for cluster scope commands
 */
enum class ClusterScopeResponseHandlerType {
  allresponses_mustbe_same,
  aggregate_all_responses,
  response_handler_none
};

/**
 * Base class for handling responses from cluster scope commands.
 * Implements the Template Method pattern where handleResponse() provides
 * the common algorithm and processAllResponses() allows customization.
 */
class BaseClusterScopeResponseHandler : public Logger::Loggable<Logger::Id::redis> {
public:
  virtual ~BaseClusterScopeResponseHandler() = default;

  /**
   * Handle a response from a shard
   * @param value the response value from upstream
   * @param shard_index the shard that sent this response (same as request index)
   * @param request the cluster scope request object (has all needed data)
   */
  void handleResponse(Common::Redis::RespValuePtr&& value, uint32_t shard_index,
                      ClusterScopeCmdRequest& request);

protected:
  // Response handler owns response tracking state
  uint32_t num_pending_responses_;
  uint32_t error_count_{0};
  std::vector<Common::Redis::RespValuePtr> pending_responses_;

  explicit BaseClusterScopeResponseHandler(uint32_t shard_count)
      : num_pending_responses_(shard_count), error_count_(0) {
    pending_responses_.reserve(shard_count);
  }

  // Template method pattern - derived classes implement specific processing
  virtual void processAllResponses(ClusterScopeCmdRequest& request) = 0;

  // Common helper methods available to all derived classes
  void storeResponse(Common::Redis::RespValuePtr&& value, uint32_t shard_index,
                     ClusterScopeCmdRequest& request);
  void sendErrorResponse(ClusterScopeCmdRequest& request, const std::string& error_message);
  void sendSuccessResponse(ClusterScopeCmdRequest& request, Common::Redis::RespValuePtr&& response);

  void handleErrorResponses(ClusterScopeCmdRequest& request);
};

/**
 * Response handler for commands where all shards must return the same response
 * Examples: CONFIG SET, FLUSHALL, SCRIPT FLUSH
 */
class AllshardSameResponseHandler : public BaseClusterScopeResponseHandler {
public:
  explicit AllshardSameResponseHandler(uint32_t shard_count)
      : BaseClusterScopeResponseHandler(shard_count) {}

protected:
  void processAllResponses(ClusterScopeCmdRequest& request) override;

private:
  bool areAllResponsesSame() const;
};

/**
 * Base class for aggregated response handlers
 */
class BaseAggregateResponseHandler : public BaseClusterScopeResponseHandler {
protected:
  explicit BaseAggregateResponseHandler(uint32_t shard_count)
      : BaseClusterScopeResponseHandler(shard_count) {}

  void processAllResponses(ClusterScopeCmdRequest& request) final;

  // Template method for specific aggregation logic
  virtual void processAggregatedResponses(ClusterScopeCmdRequest& request) = 0;
};

/**
 * Handler for integer sum aggregation (PUBSUB NUMPAT, SLOWLOG LEN)
 */
class IntegerSumAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit IntegerSumAggregateResponseHandler(uint32_t shard_count)
      : BaseAggregateResponseHandler(shard_count) {}

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;
};

/**
 * Handler for array merging (CONFIG GET, SLOWLOG GET, KEYS)
 */
class ArrayMergeAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit ArrayMergeAggregateResponseHandler(uint32_t shard_count)
      : BaseAggregateResponseHandler(shard_count) {}

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;
};

/**
 * Handler for appending entire arrays (ROLE)
 * Unlike ArrayMerge which flattens array elements, this preserves array structure
 * by appending complete arrays from each shard into a parent array.
 */
class ArrayAppendAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit ArrayAppendAggregateResponseHandler(uint32_t shard_count)
      : BaseAggregateResponseHandler(shard_count) {}

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;
};

/**
 * Handler for Aggregating INFO command responses
 */
class InfoCmdAggregateResponseHandler : public BaseAggregateResponseHandler {
public:
  explicit InfoCmdAggregateResponseHandler(uint32_t shard_count,
                                           absl::string_view info_section = "")
      : BaseAggregateResponseHandler(shard_count), info_section_(info_section) {
    initializeMetricTemplate();
  }

private:
  void processAggregatedResponses(ClusterScopeCmdRequest& request) override;

  // Metric aggregation types
  enum class AggregationType {
    First,      // Use first non-empty value
    Sum,        // Sum numeric values
    Max,        // Take maximum value
    Constant,   // Hardcoded constant
    Custom,     // Custom aggregation logic via handler function
    PostProcess // Computed during output (not aggregated from shards)
  };

  // Metric configuration structure
  struct MetricConfig {
    using CustomAggregatorFunc = void (*)(const std::string& key, absl::string_view value,
                                          MetricConfig& metric);

    std::string section;                 // e.g., "Server", "Memory"
    std::string key;                     // Metric key name
    AggregationType agg_type;            // How to aggregate
    std::string str_value;               // String value storage
    int64_t int_value;                   // Integer value storage
    CustomAggregatorFunc custom_handler; // Custom aggregation function
    std::string source_metric_for_human; // If set, convert this metric to human-readable format

    MetricConfig(const std::string& sec, const std::string& k, AggregationType type,
                 const std::string& default_val = "", CustomAggregatorFunc handler = nullptr,
                 const std::string& human_source = "")
        : section(sec), key(k), agg_type(type), str_value(default_val), int_value(0),
          custom_handler(handler), source_metric_for_human(human_source) {}
  };

  // Initialize the metric template
  void initializeMetricTemplate();

  // Process single key-value from INFO response
  void processMetric(const std::string& key, absl::string_view value);

  // Build final aggregated INFO response string
  std::string buildFinalInfoResponse() const;

  // Check if a section should be included based on info_section_ filter
  bool shouldIncludeSection(absl::string_view section) const;

  // Custom aggregation handlers
  static void aggregateKeyspaceMetric(const std::string&, absl::string_view value,
                                      MetricConfig& metric);

  // Helper to parse keyspace stats (keys=X,expires=Y,avg_ttl=Z)
  static void parseKeyspaceStats(absl::string_view value, uint64_t& keys, uint64_t& expires,
                                 uint64_t& avg_ttl);

  // Helper function to convert bytes to human-readable format
  static std::string bytesToHuman(uint64_t bytes);

  // Storage
  std::vector<MetricConfig> metric_template_;
  absl::flat_hash_map<std::string, size_t> metric_index_; // key -> index in template
  std::string info_section_ = ""; // Section filter (empty means all sections)
};

/**
 * Factory class for creating appropriate response handlers
 */
class ClusterResponseHandlerFactory {
public:
  /**
   * Create a response handler based on the command and subcommand
   * @param command_name the Redis command (e.g., "config", "info")
   * @param subcommand the Redis subcommand if applicable (e.g., "get", "set")
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createHandler(const std::string& command_name, const std::string& subcommand,
                uint32_t shard_count);

  /**
   * Create a response handler from a Redis request
   * @param request the incoming Redis request
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createFromRequest(const Common::Redis::RespValue& request, uint32_t shard_count);

private:
  /**
   * Create a specific aggregate response handler based on command and subcommand
   * @param command_name the Redis command
   * @param subcommand the Redis subcommand if applicable
   * @param shard_count the number of shards for memory pre-allocation
   * @return unique pointer to the appropriate aggregate response handler
   */
  static std::unique_ptr<BaseClusterScopeResponseHandler>
  createAggregateHandler(const std::string& command_name, const std::string& subcommand,
                         uint32_t shard_count);

  /**
   * Get the response handler type for a given command and subcommand
   * @param command_name the Redis command
   * @param subcommand the Redis subcommand if applicable
   * @return the appropriate response handler type
   */
  static ClusterScopeResponseHandlerType getResponseHandlerType(const std::string& command_name,
                                                                const std::string& subcommand = "");
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
