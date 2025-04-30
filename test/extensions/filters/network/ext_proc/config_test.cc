#include "source/extensions/filters/network/ext_proc/config.h"
#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {
namespace {

// Test the basic config setter and getter.
TEST(ConfigTest, BasicConfigTest) {
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;
  proto_config.set_failure_mode_allow(true);

  auto* processing_mode = proto_config.mutable_processing_mode();
  processing_mode->set_process_read(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
  processing_mode->set_process_write(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::STREAMED);

  Config config(proto_config);

  EXPECT_TRUE(config.failureModeAllow());

  const auto& returned_mode = config.processingMode();
  EXPECT_EQ(returned_mode.process_read(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
  EXPECT_EQ(returned_mode.process_write(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::STREAMED);
}

TEST(ConfigTest, DefaultValues) {
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;

  Config config(proto_config);

  // Test the default value for failureModeAllow (should be false by default in protobuf)
  EXPECT_FALSE(config.failureModeAllow());

  const auto& mode = config.processingMode();
  // Check default values according to protobuf definition
  EXPECT_EQ(mode.process_read(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::STREAMED);
  EXPECT_EQ(mode.process_write(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::STREAMED);
}

// Test when both read and write are set to SKIP
TEST(ConfigTest, BothSkipMode) {
  // Create a protobuf config with both read and write set to SKIP
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;

  auto* processing_mode = proto_config.mutable_processing_mode();
  processing_mode->set_process_read(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
  processing_mode->set_process_write(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  Config config(proto_config);

  const auto& mode = config.processingMode();
  EXPECT_EQ(mode.process_read(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
  EXPECT_EQ(mode.process_write(),
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
}

// Test the simple config can create the filter.
TEST(NetworkExtProcConfigTest, SimpleConfig) {
  const std::string yaml_string = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF";

  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NetworkExtProcConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb(filter_manager);
}

// Test the a config with more options can create the filter.
TEST(NetworkExtProcConfigTest, ConfigWithOptions) {
  const std::string yaml_string = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  message_timeout: 2s
  )EOF";

  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NetworkExtProcConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb(filter_manager);
}

// Test the config without a gRPC service.
TEST(NetworkExtProcConfigFactoryTest, MissingGrpcService) {
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NetworkExtProcConfigFactory factory;

  EXPECT_THROW_WITH_MESSAGE(auto cb = factory.createFilterFactoryFromProto(proto_config, context),
                            EnvoyException, "A grpc_service must be configured");
}

// Test the config with both SKIP modes.
TEST(NetworkExtProcConfigFactoryTest, BothModesSkipped) {
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;
  proto_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");

  auto* processing_mode = proto_config.mutable_processing_mode();
  processing_mode->set_process_read(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
  processing_mode->set_process_write(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NetworkExtProcConfigFactory factory;

  EXPECT_THROW_WITH_MESSAGE(auto cb = factory.createFilterFactoryFromProto(proto_config, context),
                            EnvoyException,
                            "both read and write paths are skipped, at least one must be enabled.");
}

// Test the configs with default processing modes.
TEST(NetworkExtProcConfigFactoryTest, DefaultProcessingMode) {
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor proto_config;
  proto_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NetworkExtProcConfigFactory factory;

  EXPECT_NO_THROW(auto cb = factory.createFilterFactoryFromProto(proto_config, context));
}

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
