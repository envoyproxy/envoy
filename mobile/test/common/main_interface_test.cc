#include "source/common/common/assert.h"

#include "test/common/http/common.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/main_interface.h"

using testing::_;
using testing::HasSubstr;

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

TEST(MainInterfaceTest, BasicStream) {
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
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, BUFFERED_TEST_CONFIG.c_str(), level.c_str());

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

  envoy_stream_t stream = init_stream(engine_handle);

  start_stream(engine_handle, stream, stream_cbs, false);

  send_headers(engine_handle, stream, c_headers, false);
  send_data(engine_handle, stream, c_data, false);
  send_trailers(engine_handle, stream, c_trailers);

  ASSERT_TRUE(on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  terminate_engine(engine_handle);

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(MainInterfaceTest, SendMetadata) {
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
  // only used for send_metadata.
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  envoy_http_callbacks stream_cbs{
      nullptr /* on_headers */,  nullptr /* on_data */,
      nullptr /* on_metadata */, nullptr /* on_trailers */,
      nullptr /* on_error */,    nullptr /* on_complete */,
      nullptr /* on_cancel */,   nullptr /* on_send_window_available */,
      nullptr /* context */,
  };

  envoy_stream_t stream = init_stream(engine_handle);

  start_stream(engine_handle, stream, stream_cbs, false);

  EXPECT_EQ(ENVOY_FAILURE, send_metadata(engine_handle, stream, {}));

  terminate_engine(engine_handle);

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(MainInterfaceTest, ResetStream) {
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
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

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

  envoy_stream_t stream = init_stream(engine_handle);

  start_stream(engine_handle, stream, stream_cbs, false);

  reset_stream(engine_handle, stream);

  ASSERT_TRUE(on_cancel_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  terminate_engine(engine_handle);

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(MainInterfaceTest, UsingMainInterfaceWithoutARunningEngine) {
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  Http::TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Http::Utility::toBridgeHeaders(trailers);

  EXPECT_EQ(ENVOY_FAILURE, send_headers(1, 0, c_headers, false));
  EXPECT_EQ(ENVOY_FAILURE, send_data(1, 0, c_data, false));
  EXPECT_EQ(ENVOY_FAILURE, send_trailers(1, 0, c_trailers));
  EXPECT_EQ(ENVOY_FAILURE, reset_stream(1, 0));

  // Release memory
  release_envoy_headers(c_headers);
  release_envoy_data(c_data);
  release_envoy_headers(c_trailers);
}

TEST(MainInterfaceTest, RegisterPlatformApi) {
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
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  uint64_t fake_api;
  EXPECT_EQ(ENVOY_SUCCESS, register_platform_api("api", &fake_api));

  terminate_engine(engine_handle);

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(MainInterfaceTest, InitEngineReturns1) {
  // TODO(goaway): return new handle once multiple engine support is in place.
  // https://github.com/envoyproxy/envoy-mobile/issues/332
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
  ASSERT_EQ(1, init_engine(engine_cbs, {}, {}));
  run_engine(1, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  terminate_engine(1);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(MainInterfaceTest, PreferredNetwork) {
  EXPECT_EQ(ENVOY_SUCCESS, set_preferred_network(1, ENVOY_NET_WLAN));
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
  EXPECT_EQ(ENVOY_FAILURE, record_counter_inc(1, "counter", envoy_stats_notags, 1));
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  EXPECT_EQ(ENVOY_SUCCESS, record_counter_inc(engine_handle, "counter", envoy_stats_notags, 1));

  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, SetGauge) {
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_set(1, "gauge", envoy_stats_notags, 1));
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_set(engine_handle, "gauge", envoy_stats_notags, 1));

  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, AddToGauge) {
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_add(1, "gauge", envoy_stats_notags, 30));

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_add(engine_handle, "gauge", envoy_stats_notags, 30));

  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, SubFromGauge) {
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_sub(1, "gauge", envoy_stats_notags, 30));

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  record_gauge_add(engine_handle, "gauge", envoy_stats_notags, 30);

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_sub(engine_handle, "gauge", envoy_stats_notags, 30));

  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, RecordHistogramValue) {
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
  EXPECT_EQ(ENVOY_FAILURE,
            record_histogram_value(1, "histogram", envoy_stats_notags, 99, MILLISECONDS));

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  record_histogram_value(engine_handle, "histogram", envoy_stats_notags, 99, MILLISECONDS);

  EXPECT_EQ(ENVOY_SUCCESS, record_histogram_value(engine_handle, "histogram", envoy_stats_notags,
                                                  99, MILLISECONDS));

  terminate_engine(engine_handle);
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

  envoy_logger logger{[](envoy_data data, const void* context) -> void {
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

  envoy_engine_t engine_handle = init_engine(engine_cbs, logger, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_TRUE(test_context.on_log.WaitForNotificationWithTimeout(absl::Seconds(3)));

  terminate_engine(engine_handle);
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

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

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

  terminate_engine(engine_handle);
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

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, event_tracker);
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  const auto registered_event_tracker =
      static_cast<envoy_event_tracker*>(Api::External::retrieveApi(envoy_event_tracker_api_name));
  EXPECT_TRUE(registered_event_tracker != nullptr);
  EXPECT_EQ(event_tracker.track, registered_event_tracker->track);
  EXPECT_EQ(event_tracker.context, registered_event_tracker->context);

  event_tracker.track(Bridge::Utility::makeEnvoyMap({{"foo", "bar"}}),
                      registered_event_tracker->context);

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  terminate_engine(engine_handle);
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

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, event_tracker);
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  terminate_engine(engine_handle);
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

  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, event_tracker);
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate an envoy bug by invoking an Envoy bug failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly("foo_location");

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(MainInterfaceTest, ResetConnectivityState) {
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
  envoy_engine_t engine_handle = init_engine(engine_cbs, {}, {});
  run_engine(engine_handle, MINIMAL_TEST_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_EQ(ENVOY_SUCCESS, reset_connectivity_state(engine_handle));

  terminate_engine(engine_handle);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

} // namespace Envoy
