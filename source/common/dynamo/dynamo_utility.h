#pragma once

namespace Dynamo {

class Utility {
public:
  static std::string buildPartitionStatString(const std::string& stat_prefix,
                                              const std::string& table_name,
                                              const std::string& operation,
                                              const std::string& partition_id);
};

} // Dynamo
