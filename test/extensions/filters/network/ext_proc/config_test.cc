#include "source/extensions/filters/network/ext_proc/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {
namespace {

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

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
