#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/filters/http/ext_authz/config.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/real_threads_test_helper.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::config::bootstrap::v3::Bootstrap;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class TestAsyncClientManagerImpl : public Grpc::AsyncClientManagerImpl {
public:
  TestAsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                             Server::Configuration::CommonFactoryContext& context,
                             const Grpc::StatNames& stat_names,
                             const Bootstrap::GrpcAsyncClientManagerConfig& config)
      : Grpc::AsyncClientManagerImpl(cm, tls, context, stat_names, config) {}
  absl::StatusOr<Grpc::AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) override {
    return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
  }
};

class ExtAuthzFilterTest : public Event::TestUsingSimulatedTime,
                           public Thread::RealThreadsTestHelper,
                           public testing::Test {
public:
  ExtAuthzFilterTest() : RealThreadsTestHelper(5), stat_names_(symbol_table_) {
    ON_CALL(context_.server_factory_context_, threadLocal())
        .WillByDefault(testing::ReturnRef(tls()));
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(testing::ReturnRef(api()));
    runOnMainBlocking([&]() {
      async_client_manager_ = std::make_unique<TestAsyncClientManagerImpl>(
          context_.server_factory_context_.cluster_manager_, tls(),
          context_.server_factory_context_, stat_names_, Bootstrap::GrpcAsyncClientManagerConfig());
    });
  }

  ~ExtAuthzFilterTest() override {
    // Reset the async client manager before shutdown threading.
    // Because its dtor will try to post to event loop to clear thread local slot.
    runOnMainBlocking([&]() { async_client_manager_.reset(); });
    // TODO(chaoqin-li1123): clean this up when we figure out how to free the threading resources in
    // RealThreadsTestHelper.
    shutdownThreading();
    exitThreads();
  }

  Http::FilterFactoryCb createFilterFactory(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& ext_authz_config) {
    // Delegate call to mock async client manager to real async client manager.
    ON_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
            getOrCreateRawAsyncClientWithHashKey(_, _, _))
        .WillByDefault(
            Invoke([&](const Envoy::Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                       Stats::Scope& scope, bool skip_cluster_check) {
              return async_client_manager_->getOrCreateRawAsyncClientWithHashKey(
                  config_with_hash_key, scope, skip_cluster_check);
            }));
    ExtAuthzFilterConfig factory;
    return factory.createFilterFactoryFromProto(ext_authz_config, "stats", context_).value();
  }

  Http::StreamFilterSharedPtr createFilterFromFilterFactory(Http::FilterFactoryCb filter_factory) {
    StrictMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;

    Http::StreamFilterSharedPtr filter;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
    filter_factory(filter_callbacks);
    return filter;
  }

private:
  Stats::SymbolTableImpl symbol_table_;
  Grpc::StatNames stat_names_;

protected:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::unique_ptr<TestAsyncClientManagerImpl> async_client_manager_;
};

class ExtAuthzFilterHttpTest : public ExtAuthzFilterTest {
public:
  void testFilterFactory(const std::string& ext_authz_config_yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz_config;
    Http::FilterFactoryCb filter_factory;
    // Load config and create filter factory in main thread.
    runOnMainBlocking([&]() {
      TestUtility::loadFromYaml(ext_authz_config_yaml, ext_authz_config);
      filter_factory = createFilterFactory(ext_authz_config);
    });

    // Create filter from filter factory per thread.
    for (int i = 0; i < 5; i++) {
      runOnAllWorkersBlocking([&, filter_factory]() {
        Http::StreamFilterSharedPtr filter = createFilterFromFilterFactory(filter_factory);
        EXPECT_NE(filter, nullptr);
      });
    }
  }
};

TEST_F(ExtAuthzFilterHttpTest, ExtAuthzFilterFactoryTestHttp) {
  const std::string ext_authz_config_yaml = R"EOF(
  stat_prefix: "wall"
  allowed_headers:
      patterns:
      - exact: baz
      - prefix: x-
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
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
  testFilterFactory(ext_authz_config_yaml);
}

TEST_F(ExtAuthzFilterHttpTest, FilterWithServerContext) {
  const std::string ext_authz_config_yaml = R"EOF(
  stat_prefix: "wall"
  allowed_headers:
      patterns:
      - exact: baz
      - prefix: x-
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
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
  TestUtility::loadFromYaml(ext_authz_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteGrpcServiceConfiguration) {
  const std::string per_route_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      virtual_host: "my_virtual_host"
      route_type: "high_qps"
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_high_qps"
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_FALSE(typed_config.disabled());
  EXPECT_TRUE(typed_config.grpcService().has_value());

  const auto& grpc_service = typed_config.grpcService().value();
  EXPECT_TRUE(grpc_service.has_envoy_grpc());
  EXPECT_EQ(grpc_service.envoy_grpc().cluster_name(), "ext_authz_high_qps");

  const auto& context_extensions = typed_config.contextExtensions();
  EXPECT_EQ(context_extensions.at("virtual_host"), "my_virtual_host");
  EXPECT_EQ(context_extensions.at("route_type"), "high_qps");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteHttpServiceConfiguration) {
  const std::string per_route_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      virtual_host: "my_virtual_host"
      route_type: "high_qps"
    http_service:
      server_uri:
        uri: "https://ext-authz-http.example.com"
        cluster: "ext_authz_http_cluster"
        timeout: 2s
      path_prefix: "/api/auth"
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_FALSE(typed_config.disabled());
  EXPECT_TRUE(typed_config.httpService().has_value());
  EXPECT_FALSE(typed_config.grpcService().has_value());

  const auto& http_service = typed_config.httpService().value();
  EXPECT_EQ(http_service.server_uri().uri(), "https://ext-authz-http.example.com");
  EXPECT_EQ(http_service.server_uri().cluster(), "ext_authz_http_cluster");
  EXPECT_EQ(http_service.server_uri().timeout().seconds(), 2);
  EXPECT_EQ(http_service.path_prefix(), "/api/auth");

  const auto& context_extensions = typed_config.contextExtensions();
  EXPECT_EQ(context_extensions.at("virtual_host"), "my_virtual_host");
  EXPECT_EQ(context_extensions.at("route_type"), "high_qps");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteServiceTypeSwitching) {
  // Test that we can switch service types - e.g., have gRPC in less specific and HTTP in more
  // specific
  const std::string less_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      base_setting: "from_base"
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_grpc_cluster"
  )EOF";

  const std::string more_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      override_setting: "from_override"
    http_service:
      server_uri:
        uri: "https://ext-authz-http.example.com"
        cluster: "ext_authz_http_cluster"
        timeout: 3s
      path_prefix: "/auth/check"
  )EOF";

  ExtAuthzFilterConfig factory;

  // Create less specific configuration with gRPC service
  ProtobufTypes::MessagePtr less_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(less_specific_config_yaml, *less_specific_proto);
  FilterConfigPerRoute less_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          less_specific_proto.get()));

  // Create more specific configuration with HTTP service
  ProtobufTypes::MessagePtr more_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(more_specific_config_yaml, *more_specific_proto);
  FilterConfigPerRoute more_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          more_specific_proto.get()));

  // Merge configurations - should use HTTP service from more specific config
  FilterConfigPerRoute merged_config(less_specific_config, more_specific_config);

  // Verify that HTTP service from more specific config is used (service type switching)
  EXPECT_TRUE(merged_config.httpService().has_value());
  EXPECT_FALSE(merged_config.grpcService().has_value());

  const auto& http_service = merged_config.httpService().value();
  EXPECT_EQ(http_service.server_uri().uri(), "https://ext-authz-http.example.com");
  EXPECT_EQ(http_service.server_uri().cluster(), "ext_authz_http_cluster");
  EXPECT_EQ(http_service.path_prefix(), "/auth/check");

  // Verify context extensions are properly merged (less specific preserved, more specific
  // overrides)
  const auto& context_extensions = merged_config.contextExtensions();
  EXPECT_EQ(context_extensions.size(), 2);
  EXPECT_EQ(context_extensions.at("base_setting"), "from_base");
  EXPECT_EQ(context_extensions.at("override_setting"), "from_override");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteServiceTypeSwitchingHttpToGrpc) {
  // Test that we can switch from HTTP service to gRPC service (reverse of the other test)
  const std::string less_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      base_setting: "from_base"
    http_service:
      server_uri:
        uri: "https://ext-authz-http.example.com"
        cluster: "ext_authz_http_cluster"
        timeout: 1s
      path_prefix: "/auth"
  )EOF";

  const std::string more_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      override_setting: "from_override"
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_grpc_cluster"
        authority: "ext-authz.example.com"
      timeout: 5s
  )EOF";

  ExtAuthzFilterConfig factory;

  // Create less specific configuration with HTTP service
  ProtobufTypes::MessagePtr less_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(less_specific_config_yaml, *less_specific_proto);
  FilterConfigPerRoute less_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          less_specific_proto.get()));

  // Create more specific configuration with gRPC service
  ProtobufTypes::MessagePtr more_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(more_specific_config_yaml, *more_specific_proto);
  FilterConfigPerRoute more_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          more_specific_proto.get()));

  // Merge configurations - should use gRPC service from more specific config
  FilterConfigPerRoute merged_config(less_specific_config, more_specific_config);

  // Verify that gRPC service from more specific config is used (service type switching)
  EXPECT_TRUE(merged_config.grpcService().has_value());
  EXPECT_FALSE(merged_config.httpService().has_value());

  const auto& grpc_service = merged_config.grpcService().value();
  EXPECT_TRUE(grpc_service.has_envoy_grpc());
  EXPECT_EQ(grpc_service.envoy_grpc().cluster_name(), "ext_authz_grpc_cluster");
  EXPECT_EQ(grpc_service.envoy_grpc().authority(), "ext-authz.example.com");
  EXPECT_EQ(grpc_service.timeout().seconds(), 5);

  // Verify context extensions are properly merged
  const auto& context_extensions = merged_config.contextExtensions();
  EXPECT_EQ(context_extensions.size(), 2);
  EXPECT_EQ(context_extensions.at("base_setting"), "from_base");
  EXPECT_EQ(context_extensions.at("override_setting"), "from_override");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteHttpServiceWithTimeout) {
  // Test HTTP service configuration with custom timeout
  const std::string per_route_config_yaml = R"EOF(
  check_settings:
    http_service:
      server_uri:
        uri: "https://ext-authz-custom.example.com"
        cluster: "ext_authz_custom_cluster"
        timeout: 10s
      path_prefix: "/custom/auth"
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_TRUE(typed_config.httpService().has_value());
  EXPECT_FALSE(typed_config.grpcService().has_value());

  const auto& http_service = typed_config.httpService().value();
  EXPECT_EQ(http_service.server_uri().uri(), "https://ext-authz-custom.example.com");
  EXPECT_EQ(http_service.server_uri().cluster(), "ext_authz_custom_cluster");
  EXPECT_EQ(http_service.server_uri().timeout().seconds(), 10);
  EXPECT_EQ(http_service.path_prefix(), "/custom/auth");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteGrpcServiceWithTimeout) {
  // Test gRPC service configuration with custom timeout
  const std::string per_route_config_yaml = R"EOF(
  check_settings:
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_custom_grpc"
        authority: "custom-ext-authz.example.com"
      timeout: 15s
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_TRUE(typed_config.grpcService().has_value());
  EXPECT_FALSE(typed_config.httpService().has_value());

  const auto& grpc_service = typed_config.grpcService().value();
  EXPECT_TRUE(grpc_service.has_envoy_grpc());
  EXPECT_EQ(grpc_service.envoy_grpc().cluster_name(), "ext_authz_custom_grpc");
  EXPECT_EQ(grpc_service.envoy_grpc().authority(), "custom-ext-authz.example.com");
  EXPECT_EQ(grpc_service.timeout().seconds(), 15);
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteEmptyContextExtensionsMerging) {
  // Test merging when one config has empty context extensions
  const std::string less_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      base_key: "base_value"
      shared_key: "base_shared"
    grpc_service:
      envoy_grpc:
        cluster_name: "base_cluster"
  )EOF";

  const std::string more_specific_config_yaml = R"EOF(
  check_settings:
    grpc_service:
      envoy_grpc:
        cluster_name: "specific_cluster"
  )EOF";

  ExtAuthzFilterConfig factory;

  ProtobufTypes::MessagePtr less_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(less_specific_config_yaml, *less_specific_proto);
  FilterConfigPerRoute less_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          less_specific_proto.get()));

  ProtobufTypes::MessagePtr more_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(more_specific_config_yaml, *more_specific_proto);
  FilterConfigPerRoute more_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          more_specific_proto.get()));

  FilterConfigPerRoute merged_config(less_specific_config, more_specific_config);

  // Should use gRPC service from more specific
  EXPECT_TRUE(merged_config.grpcService().has_value());
  EXPECT_EQ(merged_config.grpcService().value().envoy_grpc().cluster_name(), "specific_cluster");

  // Should preserve context extensions from less specific since more specific has none
  const auto& context_extensions = merged_config.contextExtensions();
  EXPECT_EQ(context_extensions.size(), 2);
  EXPECT_EQ(context_extensions.at("base_key"), "base_value");
  EXPECT_EQ(context_extensions.at("shared_key"), "base_shared");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteGrpcServiceConfigurationMerging) {
  // Test merging of per-route configurations
  const std::string less_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      virtual_host: "my_virtual_host"
      shared_setting: "from_less_specific"
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_default"
  )EOF";

  const std::string more_specific_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      route_type: "high_qps"
      shared_setting: "from_more_specific"
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_authz_high_qps"
  )EOF";

  ExtAuthzFilterConfig factory;

  // Create less specific configuration
  ProtobufTypes::MessagePtr less_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(less_specific_config_yaml, *less_specific_proto);
  FilterConfigPerRoute less_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          less_specific_proto.get()));

  // Create more specific configuration
  ProtobufTypes::MessagePtr more_specific_proto = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(more_specific_config_yaml, *more_specific_proto);
  FilterConfigPerRoute more_specific_config(
      *dynamic_cast<const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute*>(
          more_specific_proto.get()));

  // Merge configurations
  FilterConfigPerRoute merged_config(less_specific_config, more_specific_config);

  // Check that more specific gRPC service is used
  EXPECT_TRUE(merged_config.grpcService().has_value());
  const auto& grpc_service = merged_config.grpcService().value();
  EXPECT_TRUE(grpc_service.has_envoy_grpc());
  EXPECT_EQ(grpc_service.envoy_grpc().cluster_name(), "ext_authz_high_qps");

  // Check that context extensions are properly merged
  const auto& context_extensions = merged_config.contextExtensions();
  EXPECT_EQ(context_extensions.at("virtual_host"), "my_virtual_host");
  EXPECT_EQ(context_extensions.at("route_type"), "high_qps");
  EXPECT_EQ(context_extensions.at("shared_setting"), "from_more_specific");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteGrpcServiceConfigurationWithoutGrpcService) {
  const std::string per_route_config_yaml = R"EOF(
  check_settings:
    context_extensions:
      virtual_host: "my_virtual_host"
    disable_request_body_buffering: true
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_FALSE(typed_config.disabled());
  EXPECT_FALSE(typed_config.grpcService().has_value());

  const auto& context_extensions = typed_config.contextExtensions();
  EXPECT_EQ(context_extensions.at("virtual_host"), "my_virtual_host");
}

TEST_F(ExtAuthzFilterHttpTest, PerRouteGrpcServiceConfigurationDisabled) {
  const std::string per_route_config_yaml = R"EOF(
  disabled: true
  )EOF";

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(per_route_config_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  auto route_config = factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                              context.messageValidationVisitor());
  EXPECT_TRUE(route_config.ok());

  const auto& typed_config = dynamic_cast<const FilterConfigPerRoute&>(*route_config.value());
  EXPECT_TRUE(typed_config.disabled());
  EXPECT_FALSE(typed_config.grpcService().has_value());
}

class ExtAuthzFilterGrpcTest : public ExtAuthzFilterTest {
public:
  void testFilterFactoryAndFilterWithGrpcClient(const std::string& ext_authz_config_yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz_config;
    Http::FilterFactoryCb filter_factory;
    runOnMainBlocking([&]() {
      TestUtility::loadFromYaml(ext_authz_config_yaml, ext_authz_config);
      filter_factory = createFilterFactory(ext_authz_config);
    });

    int request_sent_per_thread = 5;
    // Initialize address instance to prepare for grpc traffic.
    initAddress();
    // Create filter from filter factory per thread and send grpc request.
    for (int i = 0; i < request_sent_per_thread; i++) {
      runOnAllWorkersBlocking([&, filter_factory]() {
        Http::StreamFilterSharedPtr filter = createFilterFromFilterFactory(filter_factory);
        testExtAuthzFilter(filter);
      });
    }
    runOnAllWorkersBlocking(
        [&]() { expectGrpcClientSentRequest(ext_authz_config, request_sent_per_thread); });
  }

private:
  void initAddress() {
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  void testExtAuthzFilter(Http::StreamFilterSharedPtr filter) {
    EXPECT_NE(filter, nullptr);
    Http::TestRequestHeaderMapImpl request_headers;
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    ON_CALL(decoder_callbacks, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    filter->setDecoderFilterCallbacks(decoder_callbacks);
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter->decodeHeaders(request_headers, false));
    std::shared_ptr<Http::StreamDecoderFilter> decoder_filter = filter;
    decoder_filter->onDestroy();
  }

  void expectGrpcClientSentRequest(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& ext_authz_config,
      int requests_sent_per_thread) {
    Envoy::Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
        Envoy::Grpc::GrpcServiceConfigWithHashKey(ext_authz_config.grpc_service());
    Grpc::RawAsyncClientSharedPtr async_client =
        async_client_manager_
            ->getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, context_.scope(), false)
            .value();
    Grpc::MockAsyncClient* mock_async_client =
        dynamic_cast<Grpc::MockAsyncClient*>(async_client.get());
    EXPECT_NE(mock_async_client, nullptr);
    // All the request in this thread should be sent through the same async client because the async
    // client is cached.
    EXPECT_EQ(mock_async_client->send_count_, requests_sent_per_thread);
  }

  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
};

TEST_F(ExtAuthzFilterGrpcTest, EnvoyGrpc) {
  const std::string ext_authz_config_yaml = R"EOF(
   grpc_service:
     envoy_grpc:
       cluster_name: test_cluster
   failure_mode_allow: false
   )EOF";
  testFilterFactoryAndFilterWithGrpcClient(ext_authz_config_yaml);
}

TEST_F(ExtAuthzFilterGrpcTest, GoogleGrpc) {
  const std::string ext_authz_config_yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  )EOF";
  testFilterFactoryAndFilterWithGrpcClient(ext_authz_config_yaml);
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
