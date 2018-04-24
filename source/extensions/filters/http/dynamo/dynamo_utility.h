#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

class Utility {
public:
  // returns example max name length, 127. This number is easily mutable in the
  // outside world, and depending on it makes this test fragile. Thus, the examples
  // in dynamo_utility_test are hard-coded to make sure
  //   `locations-sandbox-partition-test-iad-mytest-really-long-name`
  // becomes
  //   `locations-sandbox-partition-test-iad-mytest-rea`

  static uint64_t exampleMaxNameLength();

  /**
   * Creates the partition id stats string.
   * The stats format is
   * "<stat_prefix>table.<table_name>.capacity.<operation>.__partition_id=<partition_id>".
   * Partition ids and dynamodb table names can be long. To satisfy the string length,
   * we truncate in two ways:
   * 1. We only take the last 7 characters of the partition id.
   * 2. If the stats string with <table_name> is longer than the stats MAX_NAME_SIZE, we will
   * truncate the table name to
   * fit the size requirements.
   */
  static std::string buildPartitionStatString(const std::string& stat_prefix,
                                              const std::string& table_name,
                                              const std::string& operation,
                                              const std::string& partition_id);
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
