#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/network/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

namespace {
void expectCorrectProto(envoy::config::core::v3::ApiVersion api_version) {
  std::unique_ptr<TestDeprecatedV2Api> _deprecated_v2_api;
  if (api_version != envoy::config::core::v3::ApiVersion::V3) {
    _deprecated_v2_api = std::make_unique<TestDeprecatedV2Api>();
  }
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  stat_prefix: name
  transport_api_version: {}
)EOF";

  ExtAuthzConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(
      fmt::format(yaml, TestUtility::getVersionStringFromApiVersion(api_version)), *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  testing::StrictMock<Server::Configuration::MockServerFactoryContext> server_context;
  EXPECT_CALL(context, getServerFactoryContext())
      .WillRepeatedly(testing::ReturnRef(server_context));
  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}
} // namespace

TEST(ExtAuthzFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  testing::StrictMock<Server::Configuration::MockServerFactoryContext> server_context;
  EXPECT_CALL(context, getServerFactoryContext())
      .WillRepeatedly(testing::ReturnRef(server_context));
  envoy::extensions::filters::network::ext_authz::v3::ExtAuthz config;
  config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  EXPECT_THROW(ExtAuthzConfigFactory().createFilterFactoryFromProto(config, context),
               ProtoValidationException);
}

TEST(ExtAuthzFilterConfigTest, ExtAuthzCorrectProto) {
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
  expectCorrectProto(envoy::config::core::v3::ApiVersion::AUTO);
  expectCorrectProto(envoy::config::core::v3::ApiVersion::V2);
#endif
  expectCorrectProto(envoy::config::core::v3::ApiVersion::V3);
}

// Test that the deprecated extension name still functions.
TEST(ExtAuthzConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.ext_authz";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
