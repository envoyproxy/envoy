#pragma once

#include <string>

namespace Envoy {
namespace Dynamo {

class Utility {
public:
  /*
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
} // namespace Envoy
