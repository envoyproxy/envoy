#pragma once

#include <cstdint>
#include <string>

#include "source/extensions/filters/network/redis_proxy/cluster_response_handler.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

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

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
