#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/filters/http/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/real_threads_test_helper.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

class TestAsyncClientManagerImpl : public Grpc::AsyncClientManagerImpl {
public:
  TestAsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                             TimeSource& time_source, Api::Api& api,
                             const Grpc::StatNames& stat_names)
      : Grpc::AsyncClientManagerImpl(cm, tls, time_source, api, stat_names) {}
  Grpc::AsyncClientFactoryPtr factoryForGrpcService(const envoy::config::core::v3::GrpcService&,
                                                    Stats::Scope&, bool) override {
    return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
  }
};

class ExtAuthzFilterTest : public Event::TestUsingSimulatedTime,
                           public Thread::RealThreadsTestHelper,
                           public testing::Test {
public:
  ExtAuthzFilterTest() : RealThreadsTestHelper(5), stat_names_(symbol_table_) {
    runOnMainBlocking([&]() {
      async_client_manager_ = std::make_unique<TestAsyncClientManagerImpl>(
          context_.cluster_manager_, tls(), api().timeSource(), api(), stat_names_);
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
    ON_CALL(context_, getServerFactoryContext()).WillByDefault(testing::ReturnRef(server_context_));
    ON_CALL(context_.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService& config,
                                  Stats::Scope& scope, bool skip_cluster_check) {
          return async_client_manager_->getOrCreateRawAsyncClient(config, scope,
                                                                  skip_cluster_check);
        }));
    ExtAuthzFilterConfig factory;
    return factory.createFilterFactoryFromProto(ext_authz_config, "stats", context_);
  }

  Http::StreamFilterSharedPtr createFilterFromFilterFactory(Http::FilterFactoryCb filter_factory) {
    StrictMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;

    Http::StreamFilterSharedPtr filter;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
    filter_factory(filter_callbacks);
    return filter;
  }

private:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
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
  testFilterFactory(ext_authz_config_yaml);
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
    Grpc::RawAsyncClientSharedPtr async_client = async_client_manager_->getOrCreateRawAsyncClient(
        ext_authz_config.grpc_service(), context_.scope(), false);
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
   transport_api_version: V3
   grpc_service:
     envoy_grpc:
       cluster_name: test_cluster
   failure_mode_allow: false
   )EOF";
  testFilterFactoryAndFilterWithGrpcClient(ext_authz_config_yaml);
}

TEST_F(ExtAuthzFilterGrpcTest, GoogleGrpc) {
  const std::string ext_authz_config_yaml = R"EOF(
  transport_api_version: V3
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
