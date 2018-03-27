#include "extensions/filters/http/ext_authz/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

TEST(HttpExtAuthzConfigTest, ExtAuthzCorrectProto) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
)EOF";

  envoy::config::filter::http::ext_authz::v2::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  ExtAuthzFilterConfig factory;

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  Server::Configuration::HttpFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
