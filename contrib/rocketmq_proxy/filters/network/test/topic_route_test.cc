#include "source/common/protobuf/utility.h"

#include "absl/container/node_hash_map.h"
#include "contrib/rocketmq_proxy/filters/network/source/topic_route.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

TEST(TopicRouteTest, Serialization) {
  QueueData queue_data("broker-a", 8, 8, 6);
  ProtobufWkt::Struct doc;
  queue_data.encode(doc);

  const auto& members = doc.fields();

  ASSERT_STREQ("broker-a", members.at("brokerName").string_value().c_str());
  ASSERT_EQ(queue_data.brokerName(), members.at("brokerName").string_value());
  ASSERT_EQ(queue_data.readQueueNum(), members.at("readQueueNums").number_value());
  ASSERT_EQ(queue_data.writeQueueNum(), members.at("writeQueueNums").number_value());
  ASSERT_EQ(queue_data.perm(), members.at("perm").number_value());
}

TEST(BrokerDataTest, Serialization) {
  absl::node_hash_map<int64_t, std::string> broker_addrs;
  std::string dummy_address("127.0.0.1:10911");
  for (int64_t i = 0; i < 3; i++) {
    broker_addrs[i] = dummy_address;
  }
  std::string cluster("DefaultCluster");
  std::string broker_name("broker-a");
  BrokerData broker_data(cluster, broker_name, std::move(broker_addrs));

  ProtobufWkt::Struct doc;
  broker_data.encode(doc);

  const auto& members = doc.fields();

  ASSERT_STREQ(cluster.c_str(), members.at("cluster").string_value().c_str());
  ASSERT_STREQ(broker_name.c_str(), members.at("brokerName").string_value().c_str());
}

TEST(TopicRouteDataTest, Serialization) {
  TopicRouteData topic_route_data;

  for (int i = 0; i < 16; i++) {
    topic_route_data.queueData().push_back(QueueData("broker-a", 8, 8, 6));
  }

  std::string cluster("DefaultCluster");
  std::string broker_name("broker-a");
  std::string dummy_address("127.0.0.1:10911");

  for (int i = 0; i < 16; i++) {
    absl::node_hash_map<int64_t, std::string> broker_addrs;
    for (int64_t i = 0; i < 3; i++) {
      broker_addrs[i] = dummy_address;
    }
    topic_route_data.brokerData().emplace_back(
        BrokerData(cluster, broker_name, std::move(broker_addrs)));
  }
  ProtobufWkt::Struct doc;
  EXPECT_NO_THROW(topic_route_data.encode(doc));
  MessageUtil::getJsonStringFromMessageOrError(doc);
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
