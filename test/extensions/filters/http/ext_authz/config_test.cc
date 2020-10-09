#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/http/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

void expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion api_version) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  transport_api_version: {}
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(
      fmt::format(yaml, TestUtility::getVersionStringFromApiVersion(api_version)), *proto_config);

  testing::StrictMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, singletonManager()).Times(1);
  EXPECT_CALL(context, threadLocal()).Times(1);
  EXPECT_CALL(context, messageValidationVisitor()).Times(1);
  EXPECT_CALL(context, clusterManager()).Times(1);
  EXPECT_CALL(context, runtime()).Times(1);
  EXPECT_CALL(context, scope()).Times(2);
  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace

TEST(HttpExtAuthzConfigTest, CorrectProtoGrpc) {
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::AUTO);
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::V2);
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::V3);
}

TEST(HttpExtAuthzConfigTest, CorrectProtoHttp) {
  std::string yaml = R"EOF(
  stat_prefix: "wall"
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s

    authorization_request:
      allowed_headers:
        patterns:
        - exact: baz
        - prefix: x-
      headers_to_add:
      - key: foo
        value: bar
      - key: bar
        value: foo

    authorization_response:
      allowed_upstream_headers:
        patterns:
        - exact: baz
        - prefix: x-success
      allowed_client_headers:
        patterns:
        - exact: baz
        - prefix: x-fail
      allowed_upstream_headers_to_append:
        patterns:
        - exact: baz-append
        - prefix: x-append

    path_prefix: /extauth

  failure_mode_allow: true
  with_request_body:
    max_request_bytes: 100
    pack_as_bytes: true
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  testing::StrictMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(1);
  EXPECT_CALL(context, clusterManager()).Times(1);
  EXPECT_CALL(context, runtime()).Times(1);
  EXPECT_CALL(context, scope()).Times(1);
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  testing::StrictMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

// Test that setting the use_alpha proto field throws.
TEST(HttpExtAuthzConfigTest, DEPRECATED_FEATURE_TEST(UseAlphaFieldIsNoLongerSupported)) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.deprecated_features:envoy.extensions.filters.http.ext_authz.v3.ExtAuthz.hidden_"
        "envoy_deprecated_use_alpha",
        "true"}});

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
  proto_config.set_hidden_envoy_deprecated_use_alpha(true);

  // Trigger the throw in the Envoy gRPC branch.
  {
    testing::StrictMock<Server::Configuration::MockFactoryContext> context;
    EXPECT_CALL(context, messageValidationVisitor());
    EXPECT_CALL(context, runtime());
    EXPECT_CALL(context, scope());

    ExtAuthzFilterConfig factory;
    EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                              EnvoyException,
                              "The use_alpha field is deprecated and is no longer supported.")
  }

  // Trigger the throw in the Google gRPC branch.
  {
    auto google_grpc = new envoy::config::core::v3::GrpcService_GoogleGrpc();
    google_grpc->set_stat_prefix("grpc");
    google_grpc->set_target_uri("http://example.com");
    proto_config.mutable_grpc_service()->set_allocated_google_grpc(google_grpc);

    testing::StrictMock<Server::Configuration::MockFactoryContext> context;
    EXPECT_CALL(context, messageValidationVisitor());
    EXPECT_CALL(context, runtime());
    EXPECT_CALL(context, scope());

    ExtAuthzFilterConfig factory;
    EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                              EnvoyException,
                              "The use_alpha field is deprecated and is no longer supported.")
  }
}

// Test that the deprecated extension name still functions.
TEST(HttpExtAuthzConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.ext_authz";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
