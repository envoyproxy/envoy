#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "test/proto/helloworld.pb.h"
#include "envoy/stats/scope.h"

#include "source/extensions/filters/http/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"
#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

class ExtAuthzFilterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ExtAuthzFilterTest()
      : api_(Api::createApiForTest()), stat_names_(symbol_table_),
        method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {
    initialize();
  }
  void initialize() {
    std::string grpc_service_yaml = R"EOF(
  transport_api_version: V3
  grpc_service:
    envoy_grpc:
      cluster_name: test_cluster
  failure_mode_allow: false
  )EOF";
    EXPECT_CALL(context_, getServerFactoryContext())
        .WillRepeatedly(testing::ReturnRef(server_context_));
    ExtAuthzFilterConfig factory;
    ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
    TestUtility::loadFromYaml(grpc_service_yaml, *proto_config);
    filter_factory_ = factory.createFilterFactoryFromProto(*proto_config, "stats", context_);


    cm_.initializeThreadLocalClusters({"test_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(http_client_));
    ON_CALL(http_client_, start(_, _))
        .WillByDefault(
            Invoke([&](Http::AsyncClient::StreamCallbacks&,
                                                   const Http::AsyncClient::StreamOptions&) {
              return &http_stream_;
            }));
  }

  void testFilter() {
    async_client_manager_ =
        std::make_unique<Grpc::AsyncClientManagerImpl>(cm_, tls_, simTime(), *api_, stat_names_);
    Http::MockFilterChainFactoryCallbacks filter_callbacks;
    EXPECT_CALL(context_.cluster_manager_.async_client_manager_,
                getOrCreateRawAsyncClient(_, _, _, _))
        .WillOnce(Invoke([&](const envoy::config::core::v3::GrpcService& config,
                             Stats::Scope& scope, bool skip, Grpc::CacheOption cache_option) {
          return async_client_manager_->getOrCreateRawAsyncClient(config, scope, skip,
                                                                  cache_option);
        }));
    Http::StreamFilterSharedPtr filter;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
    filter_factory_(filter_callbacks);
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    filter->setDecoderFilterCallbacks(decoder_callbacks);
  }

  void testGrpcClient(Grpc::RawAsyncClientSharedPtr raw_client) {
    Grpc::AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client = raw_client;
    NiceMock<Grpc::MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
    auto grpc_stream =
        client->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
    EXPECT_NE(grpc_stream, nullptr);
  }

  void testGrpcClientMultiThread() {
    ON_CALL(context_.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService& config,
                                  Stats::Scope& scope, bool skip, Grpc::CacheOption cache_option) {
          auto raw_client =
              async_client_manager_->getOrCreateRawAsyncClient(config, scope, skip, cache_option);
          testGrpcClient(raw_client);
          return raw_client;
        }));

    for (int i = 0; i < num_threads_; i++) {
      dispatchers_.emplace_back(api_->allocateDispatcher(std::to_string(i)));
      tls_.registerThread(*dispatchers_[i], false);
    }
    for (int i = 0; i < num_threads_; i++) {
      // i must be explictly captured by value.
      workers_.emplace_back(api_->threadFactory().createThread(
          [&, i]() { dispatchers_[i]->run(Event::Dispatcher::RunType::RunUntilExit); }));
    }
    async_client_manager_ =
        std::make_unique<Grpc::AsyncClientManagerImpl>(cm_, tls_, simTime(), *api_, stat_names_);
    for (Event::DispatcherPtr& dispatcher : dispatchers_) {
      dispatcher->post([&]() {
        Http::MockFilterChainFactoryCallbacks filter_callbacks;
        EXPECT_CALL(filter_callbacks, addStreamFilter(_));
        filter_factory_(filter_callbacks);
      });
    }
    for (Event::DispatcherPtr& dispatcher : dispatchers_) {
      dispatcher->post([&dispatcher]() { dispatcher->exit(); });
    }
    for (Thread::ThreadPtr& worker : workers_) {
      worker->join();
    }
  }

  // void waitForSignal() {}

  // void sendSignal() {}

  ~ExtAuthzFilterTest() { tls_.shutdownGlobalThreading(); }

private:
  NiceMock<Upstream::MockClusterManager> cm_;
  ThreadLocal::InstanceImpl tls_;
  Api::ApiPtr api_;
  Stats::SymbolTableImpl symbol_table_;
  Grpc::StatNames stat_names_;


  std::unique_ptr<Grpc::AsyncClientManagerImpl> async_client_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Http::FilterFactoryCb filter_factory_;
  std::vector<Event::DispatcherPtr> dispatchers_;
  std::vector<Thread::ThreadPtr> workers_;

  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Http::MockAsyncClientStream> http_stream_;
  static constexpr int num_threads_ = 5;
};

TEST_F(ExtAuthzFilterTest, FilterTest) { testFilter(); }

TEST_F(ExtAuthzFilterTest, GrpcClientTest) { testGrpcClientMultiThread(); }

namespace {

void expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion api_version,
                            std::string const& grpc_service_yaml) {
  std::unique_ptr<TestDeprecatedV2Api> _deprecated_v2_api;
  if (api_version != envoy::config::core::v3::ApiVersion::V3) {
    _deprecated_v2_api = std::make_unique<TestDeprecatedV2Api>();
  }

  ExtAuthzFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(
      fmt::format(grpc_service_yaml, TestUtility::getVersionStringFromApiVersion(api_version)),
      *proto_config);

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
  transport_api_version: {}
  )EOF";
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
  // TODO(chaoqin-li1123): clean this up when we move AUTO to V3 by default.
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::AUTO, google_grpc_service_yaml);
#endif
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::V3, google_grpc_service_yaml);
}

TEST(HttpExtAuthzConfigTest, CorrectProtoEnvoyGrpc) {
  std::string envoy_grpc_service_yaml = R"EOF(
  transport_api_version: V3
  grpc_service:
    envoy_grpc:
      cluster_name: ext_authz_server
  failure_mode_allow: false
  transport_api_version: {}
  )EOF";
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
  // TODO(chaoqin-li1123): clean this up when we move AUTO to V3 by default.
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::AUTO, envoy_grpc_service_yaml);
#endif
  expectCorrectProtoGrpc(envoy::config::core::v3::ApiVersion::V3, envoy_grpc_service_yaml);
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
