#include "source/common/api/api_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "contrib/reverse_tunnel_reporter/source/reporters/event_reporter/factory.h"
#include "contrib/reverse_tunnel_reporter/source/reporters/event_reporter/reporter.h"
#include "contrib/reverse_tunnel_reporter/source/reverse_tunnel_event_types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class MockReverseTunnelReporterClient : public ReverseTunnelReporterClient {
public:
  MockReverseTunnelReporterClient() = default;
  ~MockReverseTunnelReporterClient() override = default;

  MOCK_METHOD(void, onServerInitialized, (ReverseTunnelReporterWithState*), (override));
  MOCK_METHOD(void, receiveEvents, (ReverseTunnelEvent::TunnelUpdates), (override));
};

class EventReporterTest : public testing::Test {
protected:
  void SetUp() override {
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");

    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(context_, messageValidationVisitor())
        .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

    auto mock_client1 = std::make_unique<NiceMock<MockReverseTunnelReporterClient>>();
    auto mock_client2 = std::make_unique<NiceMock<MockReverseTunnelReporterClient>>();
    mock_client1_ = mock_client1.get();
    mock_client2_ = mock_client2.get();

    std::vector<ReverseTunnelReporterClientPtr> clients;
    clients.push_back(std::move(mock_client1));
    clients.push_back(std::move(mock_client2));

    EventReporter::ConfigProto config;
    config.set_stat_prefix("test_prefix");

    reporter_ = std::make_unique<EventReporter>(context_, config, std::move(clients));
  }

  void createTestConnection(const std::string& node_id, const std::string& cluster_id,
                            const std::string& tenant_id = "tenant1") {
    reporter_->reportConnectionEvent(node_id, cluster_id, tenant_id);
  }

  void createTestDisconnection(const std::string& node_id, const std::string& cluster_id) {
    reporter_->reportDisconnectionEvent(node_id, cluster_id);
  }

  void runDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::NonBlock); }

  uint64_t getCounterValue(const std::string& name) {
    return stats_store_.counterFromString("test_prefix." + name).value();
  }

  uint64_t getGaugeValue(const std::string& name) {
    return stats_store_.gaugeFromString("test_prefix." + name, Stats::Gauge::ImportMode::Accumulate)
        .value();
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::TestUtil::TestStore stats_store_;
  NiceMock<MockReverseTunnelReporterClient>* mock_client1_;
  NiceMock<MockReverseTunnelReporterClient>* mock_client2_;
  std::unique_ptr<EventReporter> reporter_;
};

TEST_F(EventReporterTest, AddRemoveConnections) {
  EXPECT_CALL(*mock_client1_, receiveEvents(_))
      .Times(4)
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node1", updates.connections[0]->node_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node2", updates.connections[0]->node_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(0, updates.connections.size());
        EXPECT_EQ(1, updates.disconnections.size());
        EXPECT_EQ(ReverseTunnelEvent::getName("node1"), updates.disconnections[0]->name);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(0, updates.connections.size());
        EXPECT_EQ(1, updates.disconnections.size());
        EXPECT_EQ(ReverseTunnelEvent::getName("node2"), updates.disconnections[0]->name);
      }));

  EXPECT_CALL(*mock_client2_, receiveEvents(_)).Times(4);

  createTestConnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(1, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  createTestConnection("node2", "cluster2");
  runDispatcher();

  EXPECT_EQ(2, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(2, connections.size());
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(2, connections.size());
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestDisconnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(1, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  createTestDisconnection("node2", "cluster2");
  runDispatcher();

  EXPECT_EQ(2, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_full_pulls_total"));
}

TEST_F(EventReporterTest, DuplicateConnectionHandling) {
  EXPECT_CALL(*mock_client1_, receiveEvents(_))
      .Times(2)
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node1", updates.connections[0]->node_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(0, updates.connections.size());
        EXPECT_EQ(1, updates.disconnections.size());
        EXPECT_EQ(ReverseTunnelEvent::getName("node1"), updates.disconnections[0]->name);
      }));

  EXPECT_CALL(*mock_client2_, receiveEvents(_)).Times(2);

  createTestConnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(1, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  createTestConnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(2, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(1, connections.size());
  EXPECT_EQ("node1", connections[0]->node_id);
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestDisconnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(1, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(1, connections.size());
  EXPECT_EQ("node1", connections[0]->node_id);
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestDisconnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(2, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_full_pulls_total"));
}

TEST_F(EventReporterTest, PullsBeforeConnectionEvents) {
  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_full_pulls_total"));
}

TEST_F(EventReporterTest, RemoveNonExistentConnection) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::warn);
  MockLogSink sink(Envoy::Logger::Registry::getSink());

  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](absl::string_view, const spdlog::details::log_msg& msg) {
        EXPECT_EQ(spdlog::level::warn, msg.level);
      }));

  EXPECT_CALL(*mock_client1_, receiveEvents(_)).Times(0);
  EXPECT_CALL(*mock_client2_, receiveEvents(_)).Times(0);

  createTestDisconnection("nonexistent", "connection");
  runDispatcher();

  EXPECT_EQ(0, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_unique_active"));

  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  EXPECT_CALL(*mock_client1_, receiveEvents(_))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node1", updates.connections[0]->node_id);
      }));
  EXPECT_CALL(*mock_client2_, receiveEvents(_));

  createTestConnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(1, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));
}

TEST_F(EventReporterTest, OnServerInitialized) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  MockLogSink sink(Envoy::Logger::Registry::getSink());

  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](absl::string_view, const spdlog::details::log_msg& msg) {
        EXPECT_EQ(spdlog::level::info, msg.level);
      }));

  EXPECT_CALL(*mock_client1_, onServerInitialized(_));
  EXPECT_CALL(*mock_client2_, onServerInitialized(_));

  reporter_->onServerInitialized();

  EXPECT_EQ(0, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(0, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(0, getCounterValue("reverse_tunnel_full_pulls_total"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_unique_active"));
}

TEST_F(EventReporterTest, DefaultStatPrefix) {
  EventReporter::ConfigProto config;

  std::vector<ReverseTunnelReporterClientPtr> clients;
  auto mock_client1 = std::make_unique<NiceMock<MockReverseTunnelReporterClient>>();
  auto mock_client2 = std::make_unique<NiceMock<MockReverseTunnelReporterClient>>();
  mock_client1_ = mock_client1.get();
  mock_client2_ = mock_client2.get();
  clients.push_back(std::move(mock_client1));
  clients.push_back(std::move(mock_client2));

  auto default_reporter = std::make_unique<EventReporter>(context_, config, std::move(clients));

  EXPECT_CALL(*mock_client1_, receiveEvents(_));
  EXPECT_CALL(*mock_client2_, receiveEvents(_));

  default_reporter->reportConnectionEvent("node1", "cluster1", "tenant1");
  runDispatcher();

  EXPECT_EQ(
      1, stats_store_.counterFromString("reverse_tunnel_reporter.reverse_tunnel_established_total")
             .value());
  EXPECT_EQ(1, stats_store_
                   .gaugeFromString("reverse_tunnel_reporter.reverse_tunnel_active",
                                    Stats::Gauge::ImportMode::Accumulate)
                   .value());
  EXPECT_EQ(1, stats_store_
                   .gaugeFromString("reverse_tunnel_reporter.reverse_tunnel_unique_active",
                                    Stats::Gauge::ImportMode::Accumulate)
                   .value());

  auto connections = default_reporter->getAllConnections();
  EXPECT_EQ(1, connections.size());
  EXPECT_EQ(
      1, stats_store_.counterFromString("reverse_tunnel_reporter.reverse_tunnel_full_pulls_total")
             .value());
}

TEST_F(EventReporterTest, MixedScenario) {
  EXPECT_CALL(*mock_client1_, receiveEvents(_))
      .Times(4)
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node1", updates.connections[0]->node_id);
        EXPECT_EQ("tenant_A", updates.connections[0]->tenant_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node2", updates.connections[0]->node_id);
        EXPECT_EQ("tenant_B", updates.connections[0]->tenant_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(0, updates.connections.size());
        EXPECT_EQ(1, updates.disconnections.size());
        EXPECT_EQ(ReverseTunnelEvent::getName("node2"), updates.disconnections[0]->name);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node3", updates.connections[0]->node_id);
        EXPECT_EQ("tenant_C", updates.connections[0]->tenant_id);
      }));

  EXPECT_CALL(*mock_client2_, receiveEvents(_)).Times(4);

  createTestConnection("node1", "cluster1", "tenant_A");
  runDispatcher();
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  createTestConnection("node2", "cluster2", "tenant_B");
  runDispatcher();
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(2, connections.size());
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestConnection("node1", "cluster1", "tenant_A");
  runDispatcher();
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(3, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  createTestConnection("node2", "cluster2", "tenant_B");
  runDispatcher();
  EXPECT_EQ(4, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(4, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  createTestDisconnection("node1", "cluster1");
  runDispatcher();
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(3, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(2, connections.size());
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestDisconnection("node2", "cluster2");
  runDispatcher();
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  createTestDisconnection("node2", "cluster2");
  runDispatcher();
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  createTestConnection("node3", "cluster3", "tenant_C");
  runDispatcher();
  EXPECT_EQ(5, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(2, connections.size());
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_full_pulls_total"));

  EXPECT_EQ(5, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(2, getGaugeValue("reverse_tunnel_unique_active"));
}

TEST_F(EventReporterTest, LargeDuplicateCount) {
  EXPECT_CALL(*mock_client1_, receiveEvents(_))
      .Times(2)
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(1, updates.connections.size());
        EXPECT_EQ(0, updates.disconnections.size());
        EXPECT_EQ("node1", updates.connections[0]->node_id);
        EXPECT_EQ("tenant_A", updates.connections[0]->tenant_id);
      }))
      .WillOnce(Invoke([](const ReverseTunnelEvent::TunnelUpdates& updates) {
        EXPECT_EQ(0, updates.connections.size());
        EXPECT_EQ(1, updates.disconnections.size());
        EXPECT_EQ(ReverseTunnelEvent::getName("node1"), updates.disconnections[0]->name);
      }));

  EXPECT_CALL(*mock_client2_, receiveEvents(_)).Times(2);

  const int DUPLICATE_COUNT = 50;

  for (int i = 0; i < DUPLICATE_COUNT; i++) {
    createTestConnection("node1", "cluster1", "tenant_A");
    runDispatcher();
  }

  EXPECT_EQ(DUPLICATE_COUNT, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(DUPLICATE_COUNT, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  auto connections = reporter_->getAllConnections();
  EXPECT_EQ(1, connections.size());
  EXPECT_EQ(1, getCounterValue("reverse_tunnel_full_pulls_total"));

  for (int i = 0; i < DUPLICATE_COUNT - 1; i++) {
    createTestDisconnection("node1", "cluster1");
    runDispatcher();
  }

  EXPECT_EQ(DUPLICATE_COUNT - 1, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(1, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(1, connections.size());
  EXPECT_EQ(2, getCounterValue("reverse_tunnel_full_pulls_total"));

  createTestDisconnection("node1", "cluster1");
  runDispatcher();

  EXPECT_EQ(DUPLICATE_COUNT, getCounterValue("reverse_tunnel_established_total"));
  EXPECT_EQ(DUPLICATE_COUNT, getCounterValue("reverse_tunnel_closed_total"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_active"));
  EXPECT_EQ(0, getGaugeValue("reverse_tunnel_unique_active"));

  connections = reporter_->getAllConnections();
  EXPECT_EQ(0, connections.size());
  EXPECT_EQ(3, getCounterValue("reverse_tunnel_full_pulls_total"));
}

// --- Factory tests ---

class MockReverseTunnelReporterClientFactory : public ReverseTunnelReporterClientFactory {
public:
  MOCK_METHOD(ReverseTunnelReporterClientPtr, createClient,
              (Server::Configuration::ServerFactoryContext&, const Protobuf::Message&), (override));

  std::string name() const override { return "mock_client_factory"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
};

class EventReporterFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    ON_CALL(context_, messageValidationVisitor())
        .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));

    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher_));
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::TestUtil::TestStore stats_store_;
  EventReporterFactory factory_;
};

TEST_F(EventReporterFactoryTest, Name) {
  EXPECT_EQ(
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter",
      factory_.name());
}

TEST_F(EventReporterFactoryTest, CreateEmptyConfigProto) {
  auto config = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, config);
  EXPECT_NE(
      nullptr,
      dynamic_cast<
          envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig*>(
          config.get()));
}

TEST_F(EventReporterFactoryTest, CreateReporterWithRegisteredClient) {
  MockReverseTunnelReporterClientFactory mock_client_factory;
  Registry::InjectFactory<ReverseTunnelReporterClientFactory> registered(mock_client_factory);

  EXPECT_CALL(mock_client_factory, createClient(_, _))
      .WillOnce(Invoke([](Server::Configuration::ServerFactoryContext&,
                          const Protobuf::Message&) -> ReverseTunnelReporterClientPtr {
        return std::make_unique<NiceMock<MockReverseTunnelReporterClient>>();
      }));

  auto config = factory_.createEmptyConfigProto();
  auto& reporter_config = dynamic_cast<
      envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig&>(
      *config);
  reporter_config.set_stat_prefix("test");

  auto* client_entry = reporter_config.add_clients();
  client_entry->set_name("mock_client_factory");
  client_entry->mutable_typed_config()->PackFrom(Protobuf::Struct());

  auto reporter = factory_.createReporter(context_, std::move(config));
  EXPECT_NE(nullptr, reporter);
}

TEST_F(EventReporterFactoryTest, CreateClientWithUnknownFactoryThrows) {
  auto config = factory_.createEmptyConfigProto();
  auto& reporter_config = dynamic_cast<
      envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig&>(
      *config);
  reporter_config.set_stat_prefix("test");

  auto* client_entry = reporter_config.add_clients();
  client_entry->set_name("nonexistent_client_factory");
  client_entry->mutable_typed_config()->PackFrom(Protobuf::Struct());

  EXPECT_THROW_WITH_REGEX(factory_.createReporter(context_, std::move(config)), EnvoyException,
                          "Unknown Reporter Client Factory: 'nonexistent_client_factory'");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
