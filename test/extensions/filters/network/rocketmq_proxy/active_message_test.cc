#include "extensions/filters/network/rocketmq_proxy/active_message.h"
#include "extensions/filters/network/rocketmq_proxy/config.h"
#include "extensions/filters/network/rocketmq_proxy/conn_manager.h"
#include "extensions/filters/network/rocketmq_proxy/protocol.h"
#include "extensions/filters/network/rocketmq_proxy/well_known_names.h"

#include "test/extensions/filters/network/rocketmq_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class ActiveMessageTest : public testing::Test {
public:
  ActiveMessageTest()
      : stats_(RocketmqFilterStats::generateStats("test.", store_)),
        config_(rocketmq_proxy_config_, factory_context_),
        connection_manager_(config_, factory_context_.dispatcher().timeSource()) {
    connection_manager_.initializeReadFilterCallbacks(filter_callbacks_);
  }

  ~ActiveMessageTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

protected:
  ConfigImpl::RocketmqProxyConfig rocketmq_proxy_config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  RocketmqFilterStats stats_;
  ConfigImpl config_;
  ConnectionManager connection_manager_;
};

TEST_F(ActiveMessageTest, ClusterName) {
  std::string json = R"EOF(
  {
    "opaque": 1,
    "code": 35,
    "version": 1,
    "language": "JAVA",
    "serializeTypeCurrentRPC": "JSON",
    "flag": 0,
    "extFields": {
      "clientID": "SampleClient_01",
      "producerGroup": "PG_Example_01",
      "consumerGroup": "CG_001"
    }
  }
  )EOF";

  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;
  auto cmd = Decoder::decode(buffer, underflow, has_error);
  EXPECT_FALSE(underflow);
  EXPECT_FALSE(has_error);

  ActiveMessage activeMessage(connection_manager_, std::move(cmd));
  EXPECT_FALSE(activeMessage.metadata()->hasTopicName());
}

TEST_F(ActiveMessageTest, FillBrokerData) {

  std::unordered_map<int64_t, std::string> address;
  address.emplace(0, "1.2.3.4:10911");
  BrokerData broker_data("DefaultCluster", "broker-a", std::move(address));

  std::vector<BrokerData> list;
  list.push_back(broker_data);

  ActiveMessage::fillBrokerData(list, "DefaultCluster", "broker-a", 1, "localhost:10911");
  ActiveMessage::fillBrokerData(list, "DefaultCluster", "broker-a", 0, "localhost:10911");
  EXPECT_EQ(1, list.size());
  for (auto& it : list) {
    auto& address = it.brokerAddresses();
    EXPECT_EQ(2, address.size());
    EXPECT_STREQ("1.2.3.4:10911", address[0].c_str());
  }
}

TEST_F(ActiveMessageTest, FillAckMessageDirectiveSuccess) {
  RemotingCommandPtr cmd = std::make_unique<RemotingCommand>();
  ActiveMessage active_message(connection_manager_, std::move(cmd));

  Buffer::OwnedImpl buffer;
  // frame length
  buffer.writeBEInt<int32_t>(98);

  // magic code
  buffer.writeBEInt<int32_t>(enumToSignedInt(MessageVersion::V1));

  // body CRC
  buffer.writeBEInt<int32_t>(1);

  // queue Id
  buffer.writeBEInt<int32_t>(2);

  // flag
  buffer.writeBEInt<int32_t>(3);

  // queue offset
  buffer.writeBEInt<int64_t>(4);

  // physical offset
  buffer.writeBEInt<int64_t>(5);

  // system flag
  buffer.writeBEInt<int32_t>(6);

  // born timestamp
  buffer.writeBEInt<int64_t>(7);

  // born host
  buffer.writeBEInt<int32_t>(8);

  // born host port
  buffer.writeBEInt<int32_t>(9);

  // store timestamp
  buffer.writeBEInt<int64_t>(10);

  // store host address ip:port --> long
  Network::Address::Ipv4Instance host_address("127.0.0.1", 10911);
  const sockaddr_in* sock_addr = reinterpret_cast<const sockaddr_in*>(host_address.sockAddr());
  buffer.writeBEInt<int32_t>(sock_addr->sin_addr.s_addr);
  buffer.writeBEInt<int32_t>(sock_addr->sin_port);

  // re-consume times
  buffer.writeBEInt<int32_t>(11);

  // transaction offset
  buffer.writeBEInt<int64_t>(12);

  // body size
  buffer.writeBEInt<int32_t>(0);

  const std::string topic = "TopicTest";

  // topic length
  buffer.writeBEInt<int8_t>(topic.length());

  // topic data
  buffer.add(topic);

  AckMessageDirective directive("broker-a", 0, connection_manager_.timeSource().monotonicTime());
  const std::string group = "Group";
  active_message.fillAckMessageDirective(buffer, group, topic, directive);

  const std::string fake_topic = "FakeTopic";
  active_message.fillAckMessageDirective(buffer, group, fake_topic, directive);

  EXPECT_EQ(connection_manager_.getAckDirectiveTableForTest().size(), 1);
}

TEST_F(ActiveMessageTest, RecordPopRouteInfo) {
  auto host_description = new NiceMock<Upstream::MockHostDescription>();

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ProtobufWkt::Struct topic_route_data;
  auto* fields = topic_route_data.mutable_fields();

  std::string broker_name = "broker-a";
  int32_t broker_id = 0;

  (*fields)[RocketmqConstants::get().ReadQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().WriteQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().ClusterName] = ValueUtil::stringValue("DefaultCluster");
  (*fields)[RocketmqConstants::get().BrokerName] = ValueUtil::stringValue(broker_name);
  (*fields)[RocketmqConstants::get().BrokerId] = ValueUtil::numberValue(broker_id);
  (*fields)[RocketmqConstants::get().Perm] = ValueUtil::numberValue(6);
  metadata->mutable_filter_metadata()->insert(Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
      NetworkFilterNames::get().RocketmqProxy, topic_route_data));

  EXPECT_CALL(*host_description, metadata()).WillRepeatedly(Return(metadata));

  Upstream::HostDescriptionConstSharedPtr host_description_ptr(host_description);

  Buffer::OwnedImpl buffer;
  BufferUtility::fillRequestBuffer(buffer, RequestCode::PopMessage);

  bool underflow = false;
  bool has_error = false;

  RemotingCommandPtr cmd = Decoder::decode(buffer, underflow, has_error);
  ActiveMessage active_message(connection_manager_, std::move(cmd));
  active_message.recordPopRouteInfo(host_description_ptr);
  auto custom_header = active_message.downstreamRequest()->typedCustomHeader<CommandCustomHeader>();
  EXPECT_EQ(custom_header->targetBrokerName(), broker_name);
  EXPECT_EQ(custom_header->targetBrokerId(), broker_id);
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
