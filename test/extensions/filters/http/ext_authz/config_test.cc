#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/http/ext_authz/config.h"

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

void expectCorrectProtoGrpc(std::string const& grpc_service_yaml) {
  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(grpc_service_yaml, *proto_config);

  testing::StrictMock<Server::Configuration::MockFactoryContext> context;
  testing::StrictMock<Server::Configuration::MockServerFactoryContext> server_context;
  EXPECT_CALL(context, getServerFactoryContext())
      .WillRepeatedly(testing::ReturnRef(server_context));
  EXPECT_CALL(context, messageValidationVisitor());
  EXPECT_CALL(context, clusterManager()).Times(2);
  EXPECT_CALL(context, runtime());
  EXPECT_CALL(context, scope()).Times(3);

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  // Expect the raw async client to be created inside the callback.
  // The creation of the filter callback is in main thread while the execution of callback is in
  // worker thread. Because of the thread local cache of async client, it must be created in worker
  // thread inside the callback.
  EXPECT_CALL(context.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _, _))
      .WillOnce(Invoke(
          [](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool, Grpc::CacheOption) {
            return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
          }));
  cb(filter_callback);

  Thread::ThreadPtr thread = Thread::threadFactoryForTest().createThread([&context, cb]() {
    Http::MockFilterChainFactoryCallbacks filter_callback;
    EXPECT_CALL(filter_callback, addStreamFilter(_));
    // Execute the filter factory callback in another thread.
    EXPECT_CALL(context.cluster_manager_.async_client_manager_,
                getOrCreateRawAsyncClient(_, _, _, _))
        .WillOnce(Invoke(
            [](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool,
               Grpc::CacheOption) { return std::make_unique<NiceMock<Grpc::MockAsyncClient>>(); }));
    cb(filter_callback);
  });
  thread->join();
}

} // namespace

TEST(HttpExtAuthzConfigTest, CorrectProtoGoogleGrpc) {
  std::string google_grpc_service_yaml = R"EOF(
  transport_api_version: V3
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  transport_api_version: V3
  )EOF";
  expectCorrectProtoGrpc(google_grpc_service_yaml);
}

TEST(HttpExtAuthzConfigTest, CorrectProtoEnvoyGrpc) {
  std::string envoy_grpc_service_yaml = R"EOF(
  transport_api_version: V3
  grpc_service:
    envoy_grpc:
      cluster_name: ext_authz_server
  failure_mode_allow: false
  transport_api_version: V3
  )EOF";
  expectCorrectProtoGrpc(envoy_grpc_service_yaml);
}

TEST(HttpExtAuthzConfigTest, CorrectProtoHttp) {
  std::string yaml = R"EOF(
  stat_prefix: "wall"
  transport_api_version: V3
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
  testing::StrictMock<Server::Configuration::MockServerFactoryContext> server_context;
  EXPECT_CALL(context, getServerFactoryContext())
      .WillRepeatedly(testing::ReturnRef(server_context));
  EXPECT_CALL(context, messageValidationVisitor());
  EXPECT_CALL(context, clusterManager());
  EXPECT_CALL(context, runtime());
  EXPECT_CALL(context, scope());
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  testing::StrictMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// Test that the deprecated extension name is disabled by default.
// TODO(zuercher): remove when envoy.deprecated_features.allow_deprecated_extension_names is removed
TEST(HttpExtAuthzConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.ext_authz";

  ASSERT_EQ(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
