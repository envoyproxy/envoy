#include "extensions/filters/http/dynamo/dynamo_utility.h"

#include <string>

#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

std::string Utility::buildPartitionStatString(const std::string& stat_prefix,
                                              const std::string& table_name,
                                              const std::string& operation,
                                              const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  std::string stats_partition_postfix =
      fmt::format(".capacity.{}.__partition_id={}", operation,
                  partition_id.substr(partition_id.size() - 7, partition_id.size()));
  std::string stats_table_prefix = fmt::format("{}table.{}", stat_prefix, table_name);
  return fmt::format("{}{}", stats_table_prefix, stats_partition_postfix);
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
