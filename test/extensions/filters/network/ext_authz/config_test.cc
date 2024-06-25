#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/network/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
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
void expectCorrectProto() {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  stat_prefix: name
)EOF";

  ExtAuthzConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_.async_client_manager_,
              factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}
} // namespace

TEST(ExtAuthzFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::extensions::filters::network::ext_authz::v3::ExtAuthz config;
  config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  EXPECT_THROW(ExtAuthzConfigFactory().createFilterFactoryFromProto(config, context).IgnoreError(),
               ProtoValidationException);
}

TEST(ExtAuthzFilterConfigTest, ExtAuthzCorrectProto) { expectCorrectProto(); }

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
