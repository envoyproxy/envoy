#include "source/common/common/assert.h"

#include "test/common/http/common.h"
#include "test/common/mocks/common/mocks.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"

using testing::_;
using testing::HasSubstr;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {

typedef struct {
  absl::Notification on_engine_running;
  absl::Notification on_exit;
  absl::Notification on_log;
  absl::Notification on_logger_release;
  absl::Notification on_event;
} engine_test_context;

// This config is the minimal envoy mobile config that allows for running the engine.
const std::string MINIMAL_TEST_CONFIG = R"(
listener_manager:
    name: envoy.listener_manager_impl.api
    typed_config:
      "@type": type.googleapis.com/envoy.config.listener.v3.ApiListenerManager
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              include_attempt_count_in_response: true
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                route:
                  cluster_header: x-envoy-mobile-cluster
                  retry_policy:
                    retry_back_off: { base_interval: 0.25s, max_interval: 60s }
          http_filters:
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      overload: { global_downstream_max_connections: 50000 }
)";

const std::string BUFFERED_TEST_CONFIG = R"(
listener_manager:
    name: envoy.listener_manager_impl.api
    typed_config:
      "@type": type.googleapis.com/envoy.config.listener.v3.ApiListenerManager
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
       api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              include_attempt_count_in_response: true
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                direct_response: { status: 200 }
          http_filters:
          - name: buffer
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
              max_request_bytes: 65000
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      overload: { global_downstream_max_connections: 50000 }
)";

const std::string LEVEL_DEBUG = "debug";

// Transform C map to C++ map.
[[maybe_unused]] static inline std::map<std::string, std::string> toMap(envoy_map map) {
  std::map<std::string, std::string> new_map;
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    envoy_map_entry header = map.entries[i];
    const auto key = Data::Utility::copyToString(header.key);
    const auto value = Data::Utility::copyToString(header.value);
    new_map.insert({std::move(key), std::move(value)});
  }

  release_envoy_map(map);
  return new_map;
}

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  Http::ResponseHeaderMapPtr transformed_headers = Http::ResponseHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        Http::LowerCaseString(Data::Utility::copyToString(headers.entries[i].key)),
        Data::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

class MainInterfaceTest : public testing::Test {
public:
  void SetUp() override {
    helper_handle_ = test::SystemHelperPeer::replaceSystemHelper();
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
  }

protected:
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
};

TEST_F(MainInterfaceTest, BasicStream) {
  const std::string level = "debug";
  engine_test_context engine_cbs_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* engine_running =
                                          static_cast<engine_test_context*>(context);
                                      engine_running->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* exit = static_cast<engine_test_context*>(context);
                                      exit->on_exit.Notify();
                                    } /*on_exit*/,
                                    &engine_cbs_context /*context*/};
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));
  engine->run(BUFFERED_TEST_CONFIG.c_str(), level.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_complete_notification;
  envoy_http_callbacks stream_cbs{
      [](envoy_headers c_headers, bool end_stream, envoy_stream_intel, void*) -> void* {
        auto response_headers = toResponseHeaders(c_headers);
        EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
        EXPECT_TRUE(end_stream);
        return nullptr;
      } /* on_headers */,
      nullptr /* on_data */,
      nullptr /* on_metadata */,
      nullptr /* on_trailers */,
      nullptr /* on_error */,
      [](envoy_stream_intel, envoy_final_stream_intel, void* context) -> void* {
        auto* on_complete_notification = static_cast<absl::Notification*>(context);
        on_complete_notification->Notify();
        return nullptr;
      } /* on_complete */,
      nullptr /* on_cancel */,
      nullptr /* on_send_window_available*/,
      &on_complete_notification /* context */};
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  Http::TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Http::Utility::toBridgeHeaders(trailers);

  envoy_stream_t stream = engine->initStream();

  engine->startStream(stream, stream_cbs, false);

  engine->sendHeaders(stream, c_headers, false);
  engine->sendData(stream, c_data, false);
  engine->sendTrailers(stream, c_trailers);

  ASSERT_TRUE(on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine->terminate();

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(MainInterfaceTest, ResetStream) {
  engine_test_context engine_cbs_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* engine_running =
                                          static_cast<engine_test_context*>(context);
                                      engine_running->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* exit = static_cast<engine_test_context*>(context);
                                      exit->on_exit.Notify();
                                    } /*on_exit*/,
                                    &engine_cbs_context /*context*/};

  // There is nothing functional about the config used to run the engine, as the created stream is
  // immediately reset.
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_cancel_notification;
  envoy_http_callbacks stream_cbs{
      nullptr /* on_headers */,
      nullptr /* on_data */,
      nullptr /* on_metadata */,
      nullptr /* on_trailers */,
      nullptr /* on_error */,
      nullptr /* on_complete */,
      [](envoy_stream_intel, envoy_final_stream_intel, void* context) -> void* {
        auto* on_cancel_notification = static_cast<absl::Notification*>(context);
        on_cancel_notification->Notify();
        return nullptr;
      } /* on_cancel */,
      nullptr /* on_send_window_available */,
      &on_cancel_notification /* context */};

  envoy_stream_t stream = engine->initStream();

  engine->startStream(stream, stream_cbs, false);

  engine->cancelStream(stream);

  ASSERT_TRUE(on_cancel_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine->terminate();

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(MainInterfaceTest, RegisterPlatformApi) {
  engine_test_context engine_cbs_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* engine_running =
                                          static_cast<engine_test_context*>(context);
                                      engine_running->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* exit = static_cast<engine_test_context*>(context);
                                      exit->on_exit.Notify();
                                    } /*on_exit*/,
                                    &engine_cbs_context /*context*/};

  // Using the minimal envoy mobile config that allows for running the engine.
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  uint64_t fake_api;
  Envoy::Api::External::registerApi("api", &fake_api);

  engine->terminate();

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(EngineTest, RecordCounter) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* engine_running =
                                          static_cast<engine_test_context*>(context);
                                      engine_running->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* exit = static_cast<engine_test_context*>(context);
                                      exit->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));

  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  EXPECT_EQ(ENVOY_SUCCESS, engine->recordCounterInc("counter", envoy_stats_notags, 1));

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, Logger) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};

  envoy_logger logger{[](envoy_log_level, envoy_data data, const void* context) -> void {
                        auto* test_context =
                            static_cast<engine_test_context*>(const_cast<void*>(context));
                        release_envoy_data(data);
                        if (!test_context->on_log.HasBeenNotified()) {
                          test_context->on_log.Notify();
                        }
                      } /* log */,
                      [](const void* context) -> void {
                        auto* test_context =
                            static_cast<engine_test_context*>(const_cast<void*>(context));
                        test_context->on_logger_release.Notify();
                      } /* release */,
                      &test_context};
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, logger, {}));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_TRUE(test_context.on_log.WaitForNotificationWithTimeout(absl::Seconds(3)));

  engine->terminate();
  engine.reset();
  ASSERT_TRUE(test_context.on_logger_release.WaitForNotificationWithTimeout(absl::Seconds(3)));
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, EventTrackerRegistersDefaultAPI) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};

  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  // A default event tracker is registered in external API registry.
  const auto registered_event_tracker =
      static_cast<envoy_event_tracker*>(Api::External::retrieveApi(envoy_event_tracker_api_name));
  EXPECT_TRUE(registered_event_tracker != nullptr);
  EXPECT_TRUE(registered_event_tracker->track == nullptr);
  EXPECT_TRUE(registered_event_tracker->context == nullptr);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that no crash if the assertion fails when no real event
  // tracker is passed at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, EventTrackerRegistersAPI) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};
  envoy_event_tracker event_tracker{[](envoy_map map, const void* context) -> void {
                                      const auto new_map = toMap(map);
                                      if (new_map.count("foo") && new_map.at("foo") == "bar") {
                                        auto* test_context = static_cast<engine_test_context*>(
                                            const_cast<void*>(context));
                                        test_context->on_event.Notify();
                                      }
                                    } /*track*/,
                                    &test_context /*context*/};

  std::unique_ptr<Envoy::InternalEngine> engine(
      new Envoy::InternalEngine(engine_cbs, {}, event_tracker));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  const auto registered_event_tracker =
      static_cast<envoy_event_tracker*>(Api::External::retrieveApi(envoy_event_tracker_api_name));
  EXPECT_TRUE(registered_event_tracker != nullptr);
  EXPECT_EQ(event_tracker.track, registered_event_tracker->track);
  EXPECT_EQ(event_tracker.context, registered_event_tracker->context);

  event_tracker.track(Bridge::Utility::makeEnvoyMap({{"foo", "bar"}}),
                      registered_event_tracker->context);

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, EventTrackerRegistersAssertionFailureRecordAction) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};

  envoy_event_tracker event_tracker{
      [](envoy_map map, const void* context) -> void {
        const auto new_map = toMap(map);
        if (new_map.count("name") && new_map.at("name") == "assertion") {
          EXPECT_EQ(new_map.at("location"), "foo_location");
          auto* test_context = static_cast<engine_test_context*>(const_cast<void*>(context));
          test_context->on_event.Notify();
        }
      } /*track*/,
      &test_context /*context*/};

  std::unique_ptr<Envoy::InternalEngine> engine(
      new Envoy::InternalEngine(engine_cbs, {}, event_tracker));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, EventTrackerRegistersEnvoyBugRecordAction) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* test_context =
                                          static_cast<engine_test_context*>(context);
                                      test_context->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};

  envoy_event_tracker event_tracker{[](envoy_map map, const void* context) -> void {
                                      const auto new_map = toMap(map);
                                      if (new_map.count("name") && new_map.at("name") == "bug") {
                                        EXPECT_EQ(new_map.at("location"), "foo_location");
                                        auto* test_context = static_cast<engine_test_context*>(
                                            const_cast<void*>(context));
                                        test_context->on_event.Notify();
                                      }
                                    } /*track*/,
                                    &test_context /*context*/};

  std::unique_ptr<Envoy::InternalEngine> engine(
      new Envoy::InternalEngine(engine_cbs, {}, event_tracker));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate an envoy bug by invoking an Envoy bug failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly("foo_location");

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(MainInterfaceTest, ResetConnectivityState) {
  engine_test_context test_context{};
  envoy_engine_callbacks engine_cbs{[](void* context) -> void {
                                      auto* engine_running =
                                          static_cast<engine_test_context*>(context);
                                      engine_running->on_engine_running.Notify();
                                    } /*on_engine_running*/,
                                    [](void* context) -> void {
                                      auto* exit = static_cast<engine_test_context*>(context);
                                      exit->on_exit.Notify();
                                    } /*on_exit*/,
                                    &test_context /*context*/};
  std::unique_ptr<Envoy::InternalEngine> engine(new Envoy::InternalEngine(engine_cbs, {}, {}));
  engine->run(MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_EQ(ENVOY_SUCCESS, engine->resetConnectivityState());

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

} // namespace Envoy
