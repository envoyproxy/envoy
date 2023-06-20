#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

struct RocketmqValues {
  /**
   * All the values below are the properties of single broker in filter_metadata.
   */
  const std::string ReadQueueNum = "read_queue_num";
  const std::string WriteQueueNum = "write_queue_num";
  const std::string ClusterName = "cluster_name";
  const std::string BrokerName = "broker_name";
  const std::string BrokerId = "broker_id";
  const std::string Perm = "perm";
};

using RocketmqConstants = ConstSingleton<RocketmqValues>;

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
