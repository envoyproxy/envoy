#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/filters/http/ext_authz/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
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

class MultiThreadTest {
public:
  MultiThreadTest(size_t num_threads) : num_threads_(num_threads), api_(Api::createApiForTest()) {}
  virtual ~MultiThreadTest() = default;

  void postWorkToAllWorkers(std::function<void()> work) {

    // absl::BlockingCounter start_counter(num_threads_);
    absl::BlockingCounter end_counter(num_threads_ + 1);
    absl::Notification workers_should_fire;
    ASSERT(worker_dispatchers_.size() == num_threads_);
    for (Event::DispatcherPtr& dispatcher : worker_dispatchers_) {
      dispatcher->post([&, work]() {
        workers_should_fire.WaitForNotification();
        work();
        end_counter.DecrementCount();
      });
    }

    main_dispatcher_->post([&]() {
      // Wait until all the workers start to execute the callback.
      // start_counter.Wait();
      // Notify all the worker to continue.
      workers_should_fire.Notify();
      end_counter.DecrementCount();
    });
    end_counter.Wait();
  }

  void postWorkToMain(std::function<void()> work) {
    absl::BlockingCounter end_counter(1);
    main_dispatcher_->post([work, &end_counter]() {
      work();
      end_counter.DecrementCount();
    });
    end_counter.Wait();
  }

protected:
  void startThreading() {
    spawnMainThread();
    postWorkToMain([&]() { spawnWorkerThreads(); });
  }

  void cleanUpThreading() {
    main_dispatcher_->post([&]() {
      tls_->shutdownGlobalThreading();
      // Post exit signals and wait for thread to end.
      for (Event::DispatcherPtr& dispatcher : worker_dispatchers_) {
        dispatcher->post([&dispatcher]() { dispatcher->exit(); });
      }
      for (Thread::ThreadPtr& worker : worker_threads_) {
        worker->join();
      }
      tls_.reset();
      main_dispatcher_->exit();
    });
    main_thread_->join();
  }

  ThreadLocal::InstanceImpl& tls() { return *tls_; }

  Api::Api& api() { return *api_; }

private:
  void spawnMainThread() {
    main_dispatcher_ = api_->allocateDispatcher("main_thread");

    main_thread_ = api_->threadFactory().createThread([this]() {
      tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
      tls().registerThread(*main_dispatcher_, true);
      main_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    });
  }

  void spawnWorkerThreads() {
    absl::BlockingCounter start_counter(num_threads_);
    // Create worker dispatchers and register tls.
    for (size_t i = 0; i < num_threads_; i++) {
      worker_dispatchers_.emplace_back(api_->allocateDispatcher(std::to_string(i)));
      tls().registerThread(*worker_dispatchers_[i], false);
      // i must be explicitly captured by value.
      worker_threads_.emplace_back(api_->threadFactory().createThread([this, i, &start_counter]() {
        start_counter.DecrementCount();
        worker_dispatchers_[i]->run(Event::Dispatcher::RunType::RunUntilExit);
      }));
    }
    start_counter.Wait();
  }

  const size_t num_threads_;

  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  Api::ApiPtr api_;

  Event::DispatcherPtr main_dispatcher_;
  Thread::ThreadPtr main_thread_;
  std::vector<Event::DispatcherPtr> worker_dispatchers_;
  std::vector<Thread::ThreadPtr> worker_threads_;
};

class ExtAuthzFilterTest : public Event::TestUsingSimulatedTime,
                           public MultiThreadTest,
                           public testing::Test {
public:
  ExtAuthzFilterTest() : MultiThreadTest(5), stat_names_(symbol_table_) {}

  Http::FilterFactoryCb createFilterFactory(const std::string& ext_authz_config_yaml) {
    async_client_manager_ = std::make_unique<TestAsyncClientManagerImpl>(
        context_.cluster_manager_, tls(), api().timeSource(), api(), stat_names_);
    ON_CALL(context_, getServerFactoryContext()).WillByDefault(testing::ReturnRef(server_context_));
    ON_CALL(context_.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService& config,
                                  Stats::Scope& scope, bool skip_cluster_check,
                                  Grpc::CacheOption cache_option) {
          return async_client_manager_->getOrCreateRawAsyncClient(config, scope, skip_cluster_check,
                                                                  cache_option);
        }));
    ExtAuthzFilterConfig factory;
    ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
    TestUtility::loadFromYaml(ext_authz_config_yaml, *proto_config);
    return factory.createFilterFactoryFromProto(*proto_config, "stats", context_);
  }

  void initAddress() {
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  Http::StreamFilterSharedPtr createFilterFromFilterFactory(Http::FilterFactoryCb filter_factory) {
    StrictMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;

    Http::StreamFilterSharedPtr filter;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
    filter_factory(filter_callbacks);
    return filter;
  }

  void testExtAuthzFilter(Http::StreamFilterSharedPtr filter) {
    EXPECT_NE(filter, nullptr);
    Http::TestRequestHeaderMapImpl request_headers;
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    ON_CALL(decoder_callbacks, connection()).WillByDefault(Return(&connection_));
    filter->setDecoderFilterCallbacks(decoder_callbacks);
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter->decodeHeaders(request_headers, false));
    std::shared_ptr<Http::StreamDecoderFilter> decoder_filter = filter;
    decoder_filter->onDestroy();
  }

private:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::SymbolTableImpl symbol_table_;
  Grpc::StatNames stat_names_;

  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;

protected:
  std::unique_ptr<TestAsyncClientManagerImpl> async_client_manager_;

}; // namespace ExtAuthz

TEST_F(ExtAuthzFilterTest, ExtAuthzFilterFactoryTestHttp) {
  std::string ext_authz_config_yaml = R"EOF(
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

  startThreading();

  Http::FilterFactoryCb filter_factory;
  postWorkToMain([&]() { filter_factory = createFilterFactory(ext_authz_config_yaml); });

  postWorkToAllWorkers([&, filter_factory]() {
    Http::StreamFilterSharedPtr filter = createFilterFromFilterFactory(filter_factory);
    EXPECT_NE(filter, nullptr);
  });
  postWorkToMain([&]() { async_client_manager_.reset(); });
  cleanUpThreading();
}

TEST_F(ExtAuthzFilterTest, ExtAuthzFilterFactoryTestEnvoyGrpc) {
  std::string ext_authz_config_yaml = R"EOF(
   transport_api_version: V3
   grpc_service:
     envoy_grpc:
       cluster_name: test_cluster
   failure_mode_allow: false
   )EOF";
  initAddress();
  startThreading();
  Http::FilterFactoryCb filter_factory;
  postWorkToMain([&]() { filter_factory = createFilterFactory(ext_authz_config_yaml); });

  postWorkToAllWorkers([&, filter_factory]() {
    Http::StreamFilterSharedPtr filter = createFilterFromFilterFactory(filter_factory);
    testExtAuthzFilter(filter);
  });
  postWorkToMain([&]() { async_client_manager_.reset(); });
  cleanUpThreading();
}

TEST_F(ExtAuthzFilterTest, ExtAuthzFilterFactoryTestGoogleGrpc) {
  std::string ext_authz_config_yaml = R"EOF(
  transport_api_version: V3
  grpc_service:
    google_grpc:
      target_uri: ext_authz_server
      stat_prefix: google
  failure_mode_allow: false
  )EOF";
  initAddress();
  startThreading();
  Http::FilterFactoryCb filter_factory;
  postWorkToMain([&]() { filter_factory = createFilterFactory(ext_authz_config_yaml); });

  postWorkToAllWorkers([&, filter_factory]() {
    Http::StreamFilterSharedPtr filter = createFilterFromFilterFactory(filter_factory);
    testExtAuthzFilter(filter);
  });
  postWorkToMain([&]() { async_client_manager_.reset(); });
  cleanUpThreading();
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
