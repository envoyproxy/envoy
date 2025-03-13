#include "envoy/network/connection.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

#include "contrib/rocketmq_proxy/filters/network/source/config.h"
#include "contrib/rocketmq_proxy/filters/network/source/conn_manager.h"
#include "contrib/rocketmq_proxy/filters/network/source/constant.h"
#include "contrib/rocketmq_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

using ConfigRocketmqProxy = envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy;

class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(RocketmqProxyConfig config, Server::Configuration::MockFactoryContext& context,
                 RocketmqFilterStats& stats)
      : ConfigImpl(config, context), stats_(stats) {}

  RocketmqFilterStats& stats() override { return stats_; }

private:
  RocketmqFilterStats stats_;
};

class RocketmqConnectionManagerTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  RocketmqConnectionManagerTest()
      : stats_(RocketmqFilterStats::generateStats("test.", *store_.rootScope())) {}

  ~RocketmqConnectionManagerTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void initializeFilter() { initializeFilter(""); }

  void initializeFilter(const std::string& yaml) {
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config_);
      TestUtility::validate(proto_config_);
    }
    config_ = std::make_shared<TestConfigImpl>(proto_config_, factory_context_, stats_);
    conn_manager_ = std::make_unique<ConnectionManager>(
        config_, factory_context_.server_factory_context_.mainThreadDispatcher().timeSource());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
    conn_manager_->onNewConnection();
    current_ = factory_context_.server_factory_context_.mainThreadDispatcher()
                   .timeSource()
                   .monotonicTime();
  }

  void initializeCluster() {
    Upstream::HostVector hosts;
    hosts.emplace_back(host_);
    priority_set_.updateHosts(
        1,
        Upstream::HostSetImpl::partitionHosts(std::make_shared<Upstream::HostVector>(hosts),
                                              Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts, {}, 100);
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    ON_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
            prioritySet())
        .WillByDefault(ReturnRef(priority_set_));
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Stats::TestUtil::TestStore store_;
  RocketmqFilterStats stats_;
  ConfigRocketmqProxy proto_config_;

  std::shared_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<ConnectionManager> conn_manager_;

  Encoder encoder_;
  Decoder decoder_;

  MonotonicTime current_;

  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{
      Upstream::makeTestHost(cluster_info_, "tcp://127.0.0.1:80", simTime())};
  Upstream::PrioritySetImpl priority_set_;
};

TEST_F(RocketmqConnectionManagerTest, OnHeartbeat) {
  initializeFilter();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithDecodeError) {
  initializeFilter();

  std::string json = R"EOF(
  {
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "serializeTypeCurrentRPC": "JSON"
  }
  )EOF";

  buffer_.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer_.writeBEInt<int32_t>(json.size());
  buffer_.add(json);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithInvalidBodyJson) {
  initializeFilter();

  RemotingCommandPtr cmd = std::make_unique<RemotingCommand>();
  cmd->code(static_cast<int>(RequestCode::HeartBeat));
  std::string heartbeat_data = R"EOF({"clientID": "127})EOF";
  cmd->body().add(heartbeat_data);
  encoder_.encode(cmd, buffer_);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithBodyJsonLackofClientId) {
  initializeFilter();

  RemotingCommandPtr cmd = std::make_unique<RemotingCommand>();
  cmd->code(static_cast<int>(RequestCode::HeartBeat));
  std::string heartbeat_data = R"EOF(
  {
    "consumerDataSet": [{}]
  }
  )EOF";
  cmd->body().add(heartbeat_data);
  encoder_.encode(cmd, buffer_);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithGroupMembersMapExists) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.1@90330", *conn_manager_);
  group_member.setLastForTest(current_);
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());
  EXPECT_FALSE(group_member.expired());
  EXPECT_FALSE(group_members_map.at("test_cg").empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithGroupMembersMapExistsButExpired) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.2@90330", *conn_manager_);
  group_member.setLastForTest(current_ - std::chrono::seconds(31));
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());
  EXPECT_TRUE(group_member.expired());
  EXPECT_TRUE(group_members_map.empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithGroupMembersMapExistsButLackOfClientID) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.2@90330", *conn_manager_);
  group_member.setLastForTest(current_);
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());
  EXPECT_FALSE(group_member.expired());
  EXPECT_FALSE(group_members_map.at("test_cg").empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithDownstreamConnecitonClosed) {
  initializeFilter();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(connection, state()).WillOnce(Invoke([&]() -> Network::Connection::State {
    return Network::Connection::State::Closed;
  }));
  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(Invoke([&]() -> Network::Connection& {
    return connection;
  }));
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnHeartbeatWithPurgeDirectiveTable) {
  initializeFilter();

  std::string broker_name = "broker_name";
  int32_t broker_id = 0;
  std::chrono::milliseconds delay_0(31 * 1000);
  AckMessageDirective directive_0(broker_name, broker_id,
                                  conn_manager_->timeSource().monotonicTime() - delay_0);
  std::string directive_key_0 = "key_0";
  conn_manager_->insertAckDirective(directive_key_0, directive_0);

  std::chrono::milliseconds delay_1(29 * 1000);
  AckMessageDirective directive_1(broker_name, broker_id,
                                  conn_manager_->timeSource().monotonicTime() - delay_1);
  std::string directive_key_1 = "key_1";
  conn_manager_->insertAckDirective(directive_key_1, directive_1);

  EXPECT_EQ(2, conn_manager_->getAckDirectiveTableForTest().size());

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::HeartBeat);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.heartbeat").value());

  EXPECT_EQ(1, conn_manager_->getAckDirectiveTableForTest().size());
  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnUnregisterClient) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  BufferUtility::fillRequestBuffer(buffer_, RequestCode::UnregisterClient);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.unregister").value());
  EXPECT_TRUE(group_members_map.empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnUnregisterClientWithGroupMembersMapExists) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("test_client_id", *conn_manager_);
  group_member.setLastForTest(current_);
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::UnregisterClient);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.unregister").value());
  EXPECT_FALSE(group_member.expired());
  EXPECT_TRUE(group_members_map.empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnUnregisterClientWithGroupMembersMapExistsButExpired) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.2@90330", *conn_manager_);
  group_member.setLastForTest(current_ - std::chrono::seconds(31));
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::UnregisterClient);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.unregister").value());
  EXPECT_TRUE(group_member.expired());
  EXPECT_TRUE(group_members_map.empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest,
       OnUnregisterClientWithGroupMembersMapExistsButLackOfClientID) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.2@90330", *conn_manager_);
  group_member.setLastForTest(current_);
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::UnregisterClient);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.unregister").value());
  EXPECT_FALSE(group_member.expired());
  EXPECT_FALSE(group_members_map.empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetTopicRoute) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ProtobufWkt::Struct topic_route_data;
  auto* fields = topic_route_data.mutable_fields();
  (*fields)[RocketmqConstants::get().ReadQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().WriteQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().ClusterName] = ValueUtil::stringValue("DefaultCluster");
  (*fields)[RocketmqConstants::get().BrokerName] = ValueUtil::stringValue("broker-a");
  (*fields)[RocketmqConstants::get().BrokerId] = ValueUtil::numberValue(0);
  (*fields)[RocketmqConstants::get().Perm] = ValueUtil::numberValue(6);
  metadata->mutable_filter_metadata()->insert(Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
      NetworkFilterNames::get().RocketmqProxy, topic_route_data));
  host_->metadata(metadata);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetRouteInfoByTopic);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_topic_route").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetTopicRouteWithoutRoutes) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_another_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetRouteInfoByTopic);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_topic_route").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetTopicRouteWithoutCluster) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillRepeatedly(Return(nullptr));

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetRouteInfoByTopic);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_topic_route").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetTopicRouteInDevelopMode) {
  const std::string yaml = R"EOF(
stat_prefix: test
develop_mode: true
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Network::MockIp> ip;
  std::shared_ptr<const Network::MockResolvedAddress> instance =
      std::make_shared<Network::MockResolvedAddress>("logical", "physical");
  EXPECT_CALL(factory_context_, serverFactoryContext())
      .WillRepeatedly(ReturnRef(server_factory_context));
  EXPECT_CALL(server_factory_context, localInfo()).WillRepeatedly(ReturnRef(local_info));
  EXPECT_CALL(local_info, address()).WillRepeatedly(Return(instance));
  EXPECT_CALL(*instance, type()).WillRepeatedly(Return(Network::Address::Type::Ip));
  EXPECT_CALL(*instance, ip()).WillRepeatedly(testing::Return(&ip));
  const std::string address{"1.2.3.4"};
  EXPECT_CALL(ip, addressAsString()).WillRepeatedly(ReturnRef(address));
  EXPECT_CALL(ip, port()).WillRepeatedly(Return(1234));
  initializeFilter(yaml);

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ProtobufWkt::Struct topic_route_data;
  auto* fields = topic_route_data.mutable_fields();
  (*fields)[RocketmqConstants::get().ReadQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().WriteQueueNum] = ValueUtil::numberValue(4);
  (*fields)[RocketmqConstants::get().ClusterName] = ValueUtil::stringValue("DefaultCluster");
  (*fields)[RocketmqConstants::get().BrokerName] = ValueUtil::stringValue("broker-a");
  (*fields)[RocketmqConstants::get().BrokerId] = ValueUtil::numberValue(0);
  (*fields)[RocketmqConstants::get().Perm] = ValueUtil::numberValue(6);
  metadata->mutable_filter_metadata()->insert(Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
      NetworkFilterNames::get().RocketmqProxy, topic_route_data));
  host_->metadata(metadata);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetRouteInfoByTopic);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_topic_route").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetConsumerListByGroup) {
  initializeFilter();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetConsumerListByGroup);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_consumer_list").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnGetConsumerListByGroupWithGroupMemberMapExists) {
  initializeFilter();

  auto& group_members_map = conn_manager_->groupMembersForTest();
  std::vector<ConsumerGroupMember> group_members;
  ConsumerGroupMember group_member("127.0.0.2@90330", *conn_manager_);
  group_member.setLastForTest(current_ - std::chrono::seconds(31));
  group_members.emplace_back(group_member);
  group_members_map["test_cg"] = group_members;

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::GetConsumerListByGroup);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.get_consumer_list").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnPopMessage) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::PopMessage);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.pop_message").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnAckMessage) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::AckMessage);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.ack_message").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnData) {
  initializeFilter();

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());
  EXPECT_EQ(0U, store_.counter("test.request").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataWithEndStream) {
  initializeFilter();

  Buffer::OwnedImpl buffer;
  BufferUtility::fillRequestBuffer(buffer, RequestCode::SendMessageV2);
  bool underflow, has_error;
  RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);
  conn_manager_->createActiveMessage(request);
  EXPECT_EQ(1, conn_manager_->activeMessageList().size());
  conn_manager_->onData(buffer_, true);
  EXPECT_TRUE(conn_manager_->activeMessageList().empty());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataWithMinFrameSize) {
  initializeFilter();

  buffer_.add(std::string({'\x00', '\x00', '\x01', '\x8b'}));
  buffer_.add(std::string({'\x00', '\x00', '\x01', '\x76'}));
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataSendMessage) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::SendMessage);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.send_message_v1").value());
  EXPECT_EQ(
      1U,
      store_.gauge("test.send_message_v1_active", Stats::Gauge::ImportMode::Accumulate).value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataSendMessageV2) {
  const std::string yaml = R"EOF(
stat_prefix: test
route_config:
  name: default_route
  routes:
    - match:
        topic:
          exact: test_topic
      route:
        cluster: fake_cluster
)EOF";
  initializeFilter(yaml);
  initializeCluster();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::SendMessageV2);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.send_message_v2").value());
  EXPECT_EQ(
      1U,
      store_.gauge("test.send_message_v2_active", Stats::Gauge::ImportMode::Accumulate).value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataWithUnsupportedCode) {
  initializeFilter();

  BufferUtility::fillRequestBuffer(buffer_, RequestCode::Unsupported);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, OnDataInvalidFrameLength) {
  // Test against the invalid input where frame_length <= header_length.
  const std::string yaml = R"EOF(
  stat_prefix: test
  )EOF";
  initializeFilter(yaml);
  buffer_.add(
      std::string({'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00'}));
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  buffer_.drain(buffer_.length());
}

TEST_F(RocketmqConnectionManagerTest, ConsumerGroupMemberEqual) {
  initializeFilter();

  ConsumerGroupMember m1("abc", *conn_manager_);
  ConsumerGroupMember m2("abc", *conn_manager_);
  EXPECT_TRUE(m1 == m2);
}

TEST_F(RocketmqConnectionManagerTest, ConsumerGroupMemberLessThan) {
  initializeFilter();

  ConsumerGroupMember m1("abc", *conn_manager_);
  ConsumerGroupMember m2("def", *conn_manager_);
  EXPECT_TRUE(m1 < m2);
}

TEST_F(RocketmqConnectionManagerTest, ConsumerGroupMemberExpired) {
  initializeFilter();

  ConsumerGroupMember member("Mock", *conn_manager_);
  EXPECT_FALSE(member.expired());
  EXPECT_STREQ("Mock", member.clientId().data());
}

TEST_F(RocketmqConnectionManagerTest, ConsumerGroupMemberRefresh) {
  initializeFilter();

  ConsumerGroupMember member("Mock", *conn_manager_);
  EXPECT_FALSE(member.expired());
  member.setLastForTest(current_ - std::chrono::seconds(31));
  EXPECT_TRUE(member.expired());
  member.refresh();
  EXPECT_FALSE(member.expired());
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
