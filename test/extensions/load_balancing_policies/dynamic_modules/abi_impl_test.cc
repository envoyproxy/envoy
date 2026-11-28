#include <thread>

#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

using ::Envoy::Extensions::DynamicModules::newDynamicModule;
using ::Envoy::Extensions::DynamicModules::testSharedObjectPath;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

class DynamicModuleLbAbiTest : public testing::Test {
protected:
  DynamicModuleLbAbiTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
    ON_CALL(factory_context_.init_manager_, state())
        .WillByDefault(Return(Init::Manager::State::Initialized));
  }

  template <typename FunctionType>
  FunctionType moduleFunction(const std::string& module_name, const std::string& symbol) {
    // RTLD_NODELETE so the returned function pointer stays mapped after this handle is closed.
    auto module = newDynamicModule(testSharedObjectPath(module_name, "c"), true);
    return module.value()->getFunctionPointer<FunctionType>(symbol).value();
  }

  void resetCalloutModule() {
    moduleFunction<void (*)()>("lb_callout_timer", "lb_callout_timer_test_reset")();
  }

  int calloutDoneCount() {
    return moduleFunction<int (*)()>("lb_callout_timer",
                                     "lb_callout_timer_test_callout_done_count")();
  }

  int lastCalloutResult() {
    return moduleFunction<int (*)()>("lb_callout_timer", "lb_callout_timer_test_last_result")();
  }

  size_t lastHeadersSize() {
    return moduleFunction<size_t (*)()>("lb_callout_timer",
                                        "lb_callout_timer_test_last_headers_size")();
  }

  size_t lastBodySize() {
    return moduleFunction<size_t (*)()>("lb_callout_timer",
                                        "lb_callout_timer_test_last_body_size")();
  }

  int timerCount() {
    return moduleFunction<int (*)()>("lb_callout_timer", "lb_callout_timer_test_timer_count")();
  }

  uint64_t lastCalloutId() {
    return moduleFunction<uint64_t (*)()>("lb_callout_timer",
                                          "lb_callout_timer_test_last_callout_id")();
  }

  std::string lastHeaderKey() {
    return moduleFunction<const char* (*)()>("lb_callout_timer",
                                             "lb_callout_timer_test_last_header_key")();
  }

  std::string lastHeaderValue() {
    return moduleFunction<const char* (*)()>("lb_callout_timer",
                                             "lb_callout_timer_test_last_header_value")();
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr> load(const std::string& module_name) {
    using LbProto = envoy::extensions::load_balancing_policies::dynamic_modules::v3::
        DynamicModulesLoadBalancerConfig;
    LbProto config;
    config.mutable_dynamic_module_config()->set_name(module_name);
    config.set_lb_policy_name("test_lb");
    Factory factory;
    return factory.loadConfig(factory_context_, config);
  }

  DynamicModuleLbConfig* abiConfig(Upstream::LoadBalancerConfig& config) {
    auto* typed = dynamic_cast<TypedDynamicModuleLbConfig*>(&config);
    EXPECT_NE(nullptr, typed);
    return typed->config().get();
  }

  std::vector<envoy_dynamic_module_type_module_http_header> requiredHeaders() {
    return {
        {":method", 7, "GET", 3},
        {":path", 5, "/test", 5},
        {"host", 4, "example.com", 11},
    };
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

// A cluster plus the async-client request used to complete one callout.
struct SentCallout {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  Http::MockAsyncClientRequest request;
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  SentCallout() : request(&cluster.async_client_) {}
};

TEST_F(DynamicModuleLbAbiTest, HttpCalloutMissingHeaders) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {{"x-custom", 8, "value", 5}};
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutNullHeadersWithNonZeroSizeReturnsMissingRequiredHeaders) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders,
            envoy_dynamic_module_callback_lb_config_http_callout(abiConfig(*loaded.value()),
                                                                 &callout_id, {"test_cluster", 12},
                                                                 nullptr, 5, {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutClusterNotFound) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("missing"))
      .WillOnce(Return(nullptr));
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"missing", 7}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutBeforeServerInitializedReturnsClusterNotFound) {
  ON_CALL(factory_context_.init_manager_, state())
      .WillByDefault(Return(Init::Manager::State::Uninitialized));
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster(_)).Times(0);
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutInlineFailureDoesNotNotifyTheModule) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        callbacks.onFailure(sent.request, Http::AsyncClient::FailureReason::Reset);
        return nullptr;
      });
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
  EXPECT_EQ(0, calloutDoneCount());
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutCannotCreateRequest) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _)).WillOnce(Return(nullptr));
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutWithoutDoneHook) {
  auto loaded = load("lb_round_robin");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster(_)).Times(0);
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutSuccess) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_EQ(5000, options.timeout->count());
            EXPECT_EQ("hi", message->bodyAsString());
            EXPECT_EQ(absl::string_view("2"), message->headers().getContentLengthValue());
            sent.callbacks = &callbacks;
            return &sent.request;
          });
  auto headers = requiredHeaders();
  const char body_bytes[] = "hi";
  uint64_t callout_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {body_bytes, 2}, 5000));
  EXPECT_NE(0, callout_id);
  ASSERT_NE(nullptr, sent.callbacks);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-type", "text/plain"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(response_headers)));
  response->body().add("Hello, World!");
  Envoy::Tracing::MockSpan span;
  sent.callbacks->onBeforeFinalizeUpstreamSpan(span, nullptr);
  sent.callbacks->onSuccess(sent.request, std::move(response));

  EXPECT_EQ(1, calloutDoneCount());
  EXPECT_EQ(callout_id, lastCalloutId());
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_result_Success, lastCalloutResult());
  EXPECT_EQ(2, lastHeadersSize());
  EXPECT_EQ(":status", lastHeaderKey());
  EXPECT_EQ("200", lastHeaderValue());
  EXPECT_EQ(13, lastBodySize());
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutsConcurrentDistinctIdsAndHeaderBytes) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));

  SentCallout sent1;
  SentCallout sent2;

  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .Times(2)
      .WillOnce(Return(&sent1.cluster))
      .WillOnce(Return(&sent2.cluster));

  EXPECT_CALL(sent1.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        sent1.callbacks = &callbacks;
        return &sent1.request;
      });

  EXPECT_CALL(sent2.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        sent2.callbacks = &callbacks;
        return &sent2.request;
      });

  auto headers = requiredHeaders();
  uint64_t callout_id_1 = 0;
  uint64_t callout_id_2 = 0;

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id_1, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_NE(0, callout_id_1);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id_2, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_NE(0, callout_id_2);
  EXPECT_NE(callout_id_1, callout_id_2);

  // Complete callout 1.
  Http::ResponseHeaderMapPtr response_headers_1(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response1(new Http::ResponseMessageImpl(std::move(response_headers_1)));
  sent1.callbacks->onSuccess(sent1.request, std::move(response1));

  EXPECT_EQ(1, calloutDoneCount());
  EXPECT_EQ(callout_id_1, lastCalloutId());
  EXPECT_EQ(":status", lastHeaderKey());
  EXPECT_EQ("200", lastHeaderValue());

  // Complete callout 2 with distinct headers.
  Http::ResponseHeaderMapPtr response_headers_2(
      new Http::TestResponseHeaderMapImpl({{"x-custom", "callout_two"}}));
  Http::ResponseMessagePtr response2(new Http::ResponseMessageImpl(std::move(response_headers_2)));
  sent2.callbacks->onSuccess(sent2.request, std::move(response2));

  EXPECT_EQ(2, calloutDoneCount());
  EXPECT_EQ(callout_id_2, lastCalloutId());
  EXPECT_EQ("x-custom", lastHeaderKey());
  EXPECT_EQ("callout_two", lastHeaderValue());
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutNullCalloutIdOutSucceeds) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        sent.callbacks = &callbacks;
        return &sent.request;
      });
  auto headers = requiredHeaders();
  // Pass nullptr for callout_id_out; should succeed without crashing.
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), nullptr, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  ASSERT_NE(nullptr, sent.callbacks);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(response_headers)));
  sent.callbacks->onSuccess(sent.request, std::move(response));
  EXPECT_EQ(1, calloutDoneCount());
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutNullBodyDoesNotAddData) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ(0, message->body().length());
        EXPECT_EQ(nullptr, message->headers().ContentLength());
        sent.callbacks = &callbacks;
        return &sent.request;
      });
  auto headers = requiredHeaders();
  uint64_t callout_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 5}, 5000));
  ASSERT_NE(nullptr, sent.callbacks);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(response_headers)));
  Envoy::Tracing::MockSpan span;
  sent.callbacks->onBeforeFinalizeUpstreamSpan(span, nullptr);
  sent.callbacks->onSuccess(sent.request, std::move(response));
  EXPECT_EQ(1, calloutDoneCount());
  EXPECT_EQ(callout_id, lastCalloutId());
}

TEST_F(DynamicModuleLbAbiTest, HttpCalloutFailureIsReportedFromAPostedCallback) {
  for (const auto reason : {Http::AsyncClient::FailureReason::Reset,
                            Http::AsyncClient::FailureReason::ExceedResponseBufferLimit}) {
    for (const bool destroy_config : {false, true}) {
      resetCalloutModule();
      auto loaded = load("lb_callout_timer");
      ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
      SentCallout sent;
      EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
          .WillOnce(Return(&sent.cluster));
      EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
          .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                        const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            sent.callbacks = &callbacks;
            return &sent.request;
          });
      auto headers = requiredHeaders();
      uint64_t callout_id = 0;
      EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
                envoy_dynamic_module_callback_lb_config_http_callout(
                    abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                    headers.size(), {nullptr, 0}, 5000));
      ASSERT_NE(nullptr, sent.callbacks);

      Event::PostCb posted;
      EXPECT_CALL(factory_context_.dispatcher_, post(_)).WillOnce([&](Event::PostCb cb) {
        posted = std::move(cb);
      });
      sent.callbacks->onFailure(sent.request, reason);
      EXPECT_EQ(0, calloutDoneCount());

      if (destroy_config) {
        loaded.value().reset();
      }
      posted();
      if (destroy_config) {
        EXPECT_EQ(0, calloutDoneCount());
      } else {
        EXPECT_EQ(1, calloutDoneCount());
        EXPECT_EQ(callout_id, lastCalloutId());
        const int expected =
            reason == Http::AsyncClient::FailureReason::Reset
                ? envoy_dynamic_module_type_http_callout_result_Reset
                : envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
        EXPECT_EQ(expected, lastCalloutResult());
      }
    }
  }
}

TEST_F(DynamicModuleLbAbiTest, ConfigDestructionCancelsPendingCallouts) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        sent.callbacks = &callbacks;
        return &sent.request;
      });
  auto headers = requiredHeaders();
  uint64_t callout_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_CALL(sent.request, cancel()).WillOnce([&]() {
    Envoy::Tracing::MockSpan span;
    sent.callbacks->onBeforeFinalizeUpstreamSpan(span, nullptr);
  });
  loaded.value().reset();
  EXPECT_EQ(0, calloutDoneCount());
}

TEST_F(DynamicModuleLbAbiTest, TimerFiresAndRearms) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  auto* timer = new Event::MockTimer(&factory_context_.dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(25), _));
  envoy_dynamic_module_callback_lb_config_enable_timer(abiConfig(*loaded.value()), 25);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(25), _));
  timer->invokeCallback();
  EXPECT_EQ(1, timerCount());
}

TEST_F(DynamicModuleLbAbiTest, DisableTimerFromCallbackDoesNotRearm) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  moduleFunction<void (*)(int)>("lb_callout_timer",
                                "lb_callout_timer_test_set_disable_on_timer")(1);
  auto* timer = new Event::MockTimer(&factory_context_.dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(25), _));
  envoy_dynamic_module_callback_lb_config_enable_timer(abiConfig(*loaded.value()), 25);
  EXPECT_CALL(*timer, disableTimer());
  timer->invokeCallback();
  EXPECT_EQ(1, timerCount());
}

TEST_F(DynamicModuleLbAbiTest, EnableTimerAgainReusesTheExistingTimer) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  auto* timer = new Event::MockTimer(&factory_context_.dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(25), _));
  envoy_dynamic_module_callback_lb_config_enable_timer(abiConfig(*loaded.value()), 25);
  EXPECT_CALL(factory_context_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40), _));
  envoy_dynamic_module_callback_lb_config_enable_timer(abiConfig(*loaded.value()), 40);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40), _));
  timer->invokeCallback();
  EXPECT_EQ(1, timerCount());
}

TEST_F(DynamicModuleLbAbiTest, EnableTimerWithoutTimerHookDoesNotCreateTimer) {
  auto loaded = load("lb_round_robin");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  EXPECT_CALL(factory_context_.dispatcher_, createTimer_(_)).Times(0);
  envoy_dynamic_module_callback_lb_config_enable_timer(abiConfig(*loaded.value()), 25);
  envoy_dynamic_module_callback_lb_config_disable_timer(abiConfig(*loaded.value()));
}

TEST_F(DynamicModuleLbAbiTest, DisableTimerWhenNotArmedIsNoOp) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  EXPECT_CALL(factory_context_.dispatcher_, createTimer_(_)).Times(0);
  envoy_dynamic_module_callback_lb_config_disable_timer(abiConfig(*loaded.value()));
}

TEST_F(DynamicModuleLbAbiTest, CalloutAndTimerAfterConfigDestroyed) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  DynamicModuleLbConfigSharedPtr shared =
      dynamic_cast<TypedDynamicModuleLbConfig&>(*loaded.value()).config();
  loaded.value().reset();

  EXPECT_CALL(factory_context_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(factory_context_.dispatcher_, post(_)).Times(0);
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
            envoy_dynamic_module_callback_lb_config_http_callout(
                shared.get(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(),
                {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
  envoy_dynamic_module_callback_lb_config_enable_timer(shared.get(), 25);
  envoy_dynamic_module_callback_lb_config_disable_timer(shared.get());
  envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(shared.get());
}

// onSuccess runs before the callout is stored, so the module is not called.
TEST_F(DynamicModuleLbAbiTest, HttpCalloutInlineSuccessBeforeTrackingDoesNotNotifyTheModule) {
  resetCalloutModule();
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  SentCallout sent;
  EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(Return(&sent.cluster));
  EXPECT_CALL(sent.cluster.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        Http::ResponseHeaderMapPtr response_headers(
            new Http::TestResponseHeaderMapImpl{{":status", "200"}});
        Http::ResponseMessagePtr response(
            new Http::ResponseMessageImpl(std::move(response_headers)));
        callbacks.onSuccess(sent.request, std::move(response));
        return nullptr;
      });
  auto headers = requiredHeaders();
  uint64_t callout_id = 99;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
            envoy_dynamic_module_callback_lb_config_http_callout(
                abiConfig(*loaded.value()), &callout_id, {"test_cluster", 12}, headers.data(),
                headers.size(), {nullptr, 0}, 5000));
  EXPECT_EQ(99, callout_id);
  EXPECT_EQ(0, calloutDoneCount());
}

TEST_F(DynamicModuleLbAbiTest, ScheduleWithoutServerInitializedHookDoesNotRegisterTarget) {
  ON_CALL(factory_context_.init_manager_, state())
      .WillByDefault(Return(Init::Manager::State::Uninitialized));
  EXPECT_CALL(factory_context_.init_manager_, add(_)).Times(0);
  auto loaded = load("lb_callout_timer");
  EXPECT_THAT(loaded, IsOkAndHolds(NotNull()));
}

TEST_F(DynamicModuleLbAbiTest, ScheduleOnServerInitializedAfterArmedReturnsFalse) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  // The hook has already been armed during config creation. Calling it again returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(
      abiConfig(*loaded.value())));
}

TEST_F(DynamicModuleLbAbiTest, CalloutsAndTimersOffMainThreadTriggerBug) {
  auto loaded = load("lb_callout_timer");
  ASSERT_THAT(loaded, IsOkAndHolds(NotNull()));
  auto* config = abiConfig(*loaded.value());

  const auto run_off_main = [](std::function<void()> fn) {
    std::thread t(std::move(fn));
    t.join();
  };

  EXPECT_ENVOY_BUG(
      run_off_main([&] {
        uint64_t callout_id = 99;
        auto headers = requiredHeaders();
        EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
                  envoy_dynamic_module_callback_lb_config_http_callout(
                      config, &callout_id, {"test_cluster", 12}, headers.data(), headers.size(),
                      {nullptr, 0}, 1000));
        EXPECT_EQ(99, callout_id);
      }),
      "envoy_dynamic_module_callback_lb_config_http_callout must be called on the main thread");

  EXPECT_ENVOY_BUG(
      run_off_main([&] { envoy_dynamic_module_callback_lb_config_enable_timer(nullptr, 1000); }),
      "envoy_dynamic_module_callback_lb_config_enable_timer must be called on the main thread");

  EXPECT_ENVOY_BUG(
      run_off_main([&] { envoy_dynamic_module_callback_lb_config_disable_timer(nullptr); }),
      "envoy_dynamic_module_callback_lb_config_disable_timer must be called on the main thread");

  EXPECT_ENVOY_BUG(
      run_off_main(
          [&] { envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(nullptr); }),
      "envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized must be called on "
      "the main thread");
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
