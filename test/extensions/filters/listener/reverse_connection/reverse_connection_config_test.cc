#include "source/extensions/filters/listener/reverse_connection/config.h"
#include "source/extensions/filters/listener/reverse_connection/config_factory.h"
#include "source/extensions/filters/listener/reverse_connection/reverse_connection.h"

#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {
namespace {

TEST(ReverseConnectionConfigTest, DefaultConfig) {
  envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection proto_config;
  
  Config config(proto_config);
  
  // Test default ping wait timeout (10 seconds)
  EXPECT_EQ(config.pingWaitTimeout().count(), 10);
}

TEST(ReverseConnectionConfigTest, CustomConfig) {
  envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection proto_config;
  proto_config.set_ping_wait_timeout(google::protobuf::Duration());
  proto_config.mutable_ping_wait_timeout()->set_seconds(30);
  
  Config config(proto_config);
  
  // Test custom ping wait timeout (30 seconds)
  EXPECT_EQ(config.pingWaitTimeout().count(), 30);
}

TEST(ReverseConnectionConfigTest, ZeroTimeout) {
  envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection proto_config;
  proto_config.set_ping_wait_timeout(google::protobuf::Duration());
  proto_config.mutable_ping_wait_timeout()->set_seconds(0);
  
  Config config(proto_config);
  
  // Test zero ping wait timeout
  EXPECT_EQ(config.pingWaitTimeout().count(), 0);
}

TEST(ReverseConnectionConfigFactoryTest, TestCreateFactory) {
  const std::string yaml = R"EOF(
    ping_wait_timeout:
      seconds: 15
  )EOF";

  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, nullptr, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ReverseConnection::Filter*>(added_filter.get()), nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestCreateFactoryWithDefaultConfig) {
  const std::string yaml = R"EOF(
    {}
  )EOF";

  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, nullptr, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ReverseConnection::Filter*>(added_filter.get()), nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestCreateFactoryWithZeroTimeout) {
  const std::string yaml = R"EOF(
    ping_wait_timeout:
      seconds: 0
  )EOF";

  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, nullptr, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ReverseConnection::Filter*>(added_filter.get()), nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestCreateFactoryWithMatcher) {
  const std::string yaml = R"EOF(
    ping_wait_timeout:
      seconds: 20
  )EOF";

  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;
  
  // Create a mock filter matcher
  auto matcher = std::make_shared<Network::MockListenerFilterMatcher>();

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, matcher, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ReverseConnection::Filter*>(added_filter.get()), nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestCreateEmptyConfigProto) {
  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  
  EXPECT_NE(proto_config, nullptr);
  
  // Verify it's the correct type
  auto* reverse_connection_config = 
      dynamic_cast<envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection*>(
          proto_config.get());
  EXPECT_NE(reverse_connection_config, nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestFactoryRegistration) {
  const std::string filter_name = "envoy.filters.listener.reverse_connection";
  
  // Test that the factory is registered
  Server::Configuration::NamedListenerFilterConfigFactory* factory = 
      Registry::FactoryRegistry<Server::Configuration::NamedListenerFilterConfigFactory>::
          getFactory(filter_name);
  
  EXPECT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), filter_name);
}

TEST(ReverseConnectionConfigFactoryTest, TestFactoryWithValidation) {
  const std::string yaml = R"EOF(
    ping_wait_timeout:
      seconds: 25
      nanos: 500000000
  )EOF";

  ReverseConnectionConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor())
      .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, nullptr, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ReverseConnection::Filter*>(added_filter.get()), nullptr);
}

TEST(ReverseConnectionConfigFactoryTest, TestFactoryWithInvalidConfig) {
  // Create an invalid config by using a different message type
  auto invalid_config = std::make_unique<google::protobuf::Empty>();

  ReverseConnectionConfigFactory factory;
  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  // This should throw an exception due to invalid message type
  EXPECT_THROW(
      factory.createListenerFilterFactoryFromProto(*invalid_config, nullptr, context),
      EnvoyException);
}

} // namespace
} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy 