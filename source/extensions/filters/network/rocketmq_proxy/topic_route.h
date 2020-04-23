#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {
class QueueData {
public:
  QueueData(const std::string& broker_name, int32_t read_queue_num, int32_t write_queue_num,
            int32_t perm)
      : broker_name_(broker_name), read_queue_nums_(read_queue_num),
        write_queue_nums_(write_queue_num), perm_(perm) {}

  void encode(ProtobufWkt::Struct& data_struct);

  const std::string& brokerName() const { return broker_name_; }

  int32_t readQueueNum() const { return read_queue_nums_; }

  int32_t writeQueueNum() const { return write_queue_nums_; }

  int32_t perm() const { return perm_; }

private:
  std::string broker_name_;
  int32_t read_queue_nums_;
  int32_t write_queue_nums_;
  int32_t perm_;
};

class BrokerData {
public:
  BrokerData(const std::string& cluster, const std::string& broker_name,
             std::unordered_map<int64_t, std::string>&& broker_addrs)
      : cluster_(cluster), broker_name_(broker_name), broker_addrs_(broker_addrs) {}

  void encode(ProtobufWkt::Struct& data_struct);

  const std::string& cluster() const { return cluster_; }

  const std::string& brokerName() const { return broker_name_; }

  std::unordered_map<int64_t, std::string>& brokerAddresses() { return broker_addrs_; }

private:
  std::string cluster_;
  std::string broker_name_;
  std::unordered_map<int64_t, std::string> broker_addrs_;
};

class TopicRouteData {
public:
  void encode(ProtobufWkt::Struct& data_struct);

  TopicRouteData() = default;

  TopicRouteData(std::vector<QueueData>&& queue_data, std::vector<BrokerData>&& broker_data)
      : queue_data_(queue_data), broker_data_(broker_data) {}

  std::vector<QueueData>& queueData() { return queue_data_; }

  std::vector<BrokerData>& brokerData() { return broker_data_; }

private:
  std::vector<QueueData> queue_data_;
  std::vector<BrokerData> broker_data_;
};

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy