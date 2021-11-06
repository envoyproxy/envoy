#include "contrib/rocketmq_proxy/filters/network/source/topic_route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

void QueueData::encode(ProtobufWkt::Struct& data_struct) {
  auto* fields = data_struct.mutable_fields();

  ProtobufWkt::Value broker_name_v;
  broker_name_v.set_string_value(broker_name_);
  (*fields)["brokerName"] = broker_name_v;

  ProtobufWkt::Value read_queue_num_v;
  read_queue_num_v.set_number_value(read_queue_nums_);
  (*fields)["readQueueNums"] = read_queue_num_v;

  ProtobufWkt::Value write_queue_num_v;
  write_queue_num_v.set_number_value(write_queue_nums_);
  (*fields)["writeQueueNums"] = write_queue_num_v;

  ProtobufWkt::Value perm_v;
  perm_v.set_number_value(perm_);
  (*fields)["perm"] = perm_v;
}

void BrokerData::encode(ProtobufWkt::Struct& data_struct) {
  auto& members = *(data_struct.mutable_fields());

  ProtobufWkt::Value cluster_v;
  cluster_v.set_string_value(cluster_);
  members["cluster"] = cluster_v;

  ProtobufWkt::Value broker_name_v;
  broker_name_v.set_string_value(broker_name_);
  members["brokerName"] = broker_name_v;

  if (!broker_addrs_.empty()) {
    ProtobufWkt::Value brokerAddrsNode;
    auto& brokerAddrsMembers = *(brokerAddrsNode.mutable_struct_value()->mutable_fields());
    for (auto& entry : broker_addrs_) {
      ProtobufWkt::Value address_v;
      address_v.set_string_value(entry.second);
      brokerAddrsMembers[std::to_string(entry.first)] = address_v;
    }
    members["brokerAddrs"] = brokerAddrsNode;
  }
}

void TopicRouteData::encode(ProtobufWkt::Struct& data_struct) {
  auto* fields = data_struct.mutable_fields();

  if (!queue_data_.empty()) {
    ProtobufWkt::ListValue queue_data_list_v;
    for (auto& queueData : queue_data_) {
      queueData.encode(data_struct);
      queue_data_list_v.add_values()->mutable_struct_value()->CopyFrom(data_struct);
    }
    (*fields)["queueDatas"].mutable_list_value()->CopyFrom(queue_data_list_v);
  }

  if (!broker_data_.empty()) {
    ProtobufWkt::ListValue broker_data_list_v;
    for (auto& brokerData : broker_data_) {
      brokerData.encode(data_struct);
      broker_data_list_v.add_values()->mutable_struct_value()->CopyFrom(data_struct);
    }
    (*fields)["brokerDatas"].mutable_list_value()->CopyFrom(broker_data_list_v);
  }
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
