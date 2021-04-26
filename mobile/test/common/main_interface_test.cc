#include "test/common/http/common.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
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
} engine_test_context;

// This config is the minimal envoy mobile config that allows for running the engine.
const std::string MINIMAL_NOOP_CONFIG =
    "{\"admin\":{},\"static_resources\":{\"listeners\":[{\"name\":\"base_api_listener\","
    "\"address\":{\"socket_address\":{\"protocol\":\"TCP\",\"address\":\"0.0.0.0\",\"port_"
    "value\":10000}},\"api_listener\":{\"api_listener\":{\"@type\":\"type.googleapis.com/"
    "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager\",\"stat_"
    "prefix\":\"hcm\",\"route_config\":{\"name\":\"api_router\",\"virtual_hosts\":[{\"name\":"
    "\"api\",\"include_attempt_count_in_response\":true,\"domains\":[\"*\"],\"routes\":[{"
    "\"match\":{\"prefix\":\"/"
    "\"},\"route\":{\"cluster_header\":\"x-envoy-mobile-cluster\",\"retry_policy\":{\"retry_back_"
    "off\":{\"base_interval\":\"0.25s\",\"max_interval\":\"60s\"}}}}]}]},\"http_filters\":[{"
    "\"name\":\"envoy.router\",\"typed_config\":{\"@type\":\"type.googleapis.com/"
    "envoy.extensions.filters.http.router.v3.Router\"}}]}}}]},\"layered_runtime\":{\"layers\":[{"
    "\"name\":\"static_layer_0\",\"static_layer\":{\"overload\":{\"global_downstream_max_"
    "connections\":50000}}}]}}";

const std::string LEVEL_DEBUG = "debug";

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
  const std::string config =
      "{\"admin\":{},\"static_resources\":{\"listeners\":[{\"name\":\"base_api_listener\", "
      "\"address\":{\"socket_address\":{\"protocol\":\"TCP\",\"address\":\"0.0.0.0\",\"port_"
      "value\":10000}},\"api_listener\":{\"api_listener\":{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager\",\"stat_"
      "prefix\":\"hcm\",\"route_config\":{\"name\":\"api_router\",\"virtual_hosts\":[{\"name\":"
      "\"api\",\"include_attempt_count_in_response\":true,\"domains\":[\"*\"],\"routes\":[{"
      "\"match\":{\"prefix\":\"/"
      // This config has the buffer filter which allows the test to exercise all of the send_*
      // methods, and a direct response, which allows for simple stream completion.
      "\"},\"direct_response\":{\"status\":\"200\"}}]}]},\"http_filters\":[{\"name\":\"buffer\","
      "\"typed_config\":{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.filters.http.buffer.v3.Buffer\", \"max_request_bytes\": \"65000\"}}, "
      "{\"name\":\"envoy.router\",\"typed_config\":{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.filters.http.router.v3.Router\"}}]}}}]},\"layered_runtime\":{\"layers\":[{"
      "\"name\":\"static_layer_0\",\"static_layer\":{\"overload\":{\"global_downstream_max_"
      "connections\":50000}}}]}}";
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
  init_engine(engine_cbs, {});
  run_engine(0, config.c_str(), level.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_complete_notification;
  envoy_http_callbacks stream_cbs{
      [](envoy_headers c_headers, bool end_stream, void*) -> void* {
        auto response_headers = toResponseHeaders(c_headers);
        EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
        EXPECT_TRUE(end_stream);
        return nullptr;
      } /* on_headers */,
      nullptr /* on_data */,
      nullptr /* on_metadata */,
      nullptr /* on_trailers */,
      nullptr /* on_error */,
      [](void* context) -> void* {
        auto* on_complete_notification = static_cast<absl::Notification*>(context);
        on_complete_notification->Notify();
        return nullptr;
      } /* on_complete */,
      nullptr /* on_cancel */,
      &on_complete_notification /* context */};
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  Http::TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Http::Utility::toBridgeHeaders(trailers);

  envoy_stream_t stream = init_stream(0);

  start_stream(stream, stream_cbs);

  send_headers(stream, c_headers, false);
  send_data(stream, c_data, false);
  send_trailers(stream, c_trailers);

  ASSERT_TRUE(on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  terminate_engine(0);

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
  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  envoy_http_callbacks stream_cbs{nullptr /* on_headers */,  nullptr /* on_data */,
                                  nullptr /* on_metadata */, nullptr /* on_trailers */,
                                  nullptr /* on_error */,    nullptr /* on_complete */,
                                  nullptr /* on_cancel */,   nullptr /* context */};

  envoy_stream_t stream = init_stream(0);

  start_stream(stream, stream_cbs);

  EXPECT_EQ(ENVOY_FAILURE, send_metadata(stream, {}));

  terminate_engine(0);

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
  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_cancel_notification;
  envoy_http_callbacks stream_cbs{nullptr /* on_headers */,
                                  nullptr /* on_data */,
                                  nullptr /* on_metadata */,
                                  nullptr /* on_trailers */,
                                  nullptr /* on_error */,
                                  nullptr /* on_complete */,
                                  [](void* context) -> void* {
                                    auto* on_cancel_notification =
                                        static_cast<absl::Notification*>(context);
                                    on_cancel_notification->Notify();
                                    return nullptr;
                                  } /* on_cancel */,
                                  &on_cancel_notification /* context */};

  envoy_stream_t stream = init_stream(0);

  start_stream(stream, stream_cbs);

  reset_stream(stream);

  ASSERT_TRUE(on_cancel_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  terminate_engine(0);

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

  EXPECT_EQ(ENVOY_FAILURE, send_headers(0, c_headers, false));
  EXPECT_EQ(ENVOY_FAILURE, send_data(0, c_data, false));
  EXPECT_EQ(ENVOY_FAILURE, send_trailers(0, c_trailers));
  EXPECT_EQ(ENVOY_FAILURE, reset_stream(0));

  // Release memory
  release_envoy_headers(c_headers);
  c_data.release(c_data.context);
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
  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(
      engine_cbs_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  EXPECT_EQ(ENVOY_SUCCESS, register_platform_api("api", nullptr));

  terminate_engine(0);

  ASSERT_TRUE(engine_cbs_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST(MainInterfaceTest, InitEngineReturns1) {
  // TODO(goaway): return new handle once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
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
  ASSERT_EQ(1, init_engine(engine_cbs, {}));
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  terminate_engine(0);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(MainInterfaceTest, PreferredNetwork) {
  EXPECT_EQ(ENVOY_SUCCESS, set_preferred_network(ENVOY_NET_WLAN));
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
  EXPECT_EQ(ENVOY_FAILURE, record_counter_inc(0, "counter", envoy_stats_notags, 1));
  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  EXPECT_EQ(ENVOY_SUCCESS, record_counter_inc(0, "counter", envoy_stats_notags, 1));

  terminate_engine(0);
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_set(0, "gauge", envoy_stats_notags, 1));
  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_set(0, "gauge", envoy_stats_notags, 1));

  terminate_engine(0);
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_add(0, "gauge", envoy_stats_notags, 30));

  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_add(0, "gauge", envoy_stats_notags, 30));

  terminate_engine(0);
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
  EXPECT_EQ(ENVOY_FAILURE, record_gauge_sub(0, "gauge", envoy_stats_notags, 30));

  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  record_gauge_add(0, "gauge", envoy_stats_notags, 30);

  EXPECT_EQ(ENVOY_SUCCESS, record_gauge_sub(0, "gauge", envoy_stats_notags, 30));

  terminate_engine(0);
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
            record_histogram_value(0, "histogram", envoy_stats_notags, 99, MILLISECONDS));

  init_engine(engine_cbs, {});
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  record_histogram_value(0, "histogram", envoy_stats_notags, 99, MILLISECONDS);

  EXPECT_EQ(ENVOY_SUCCESS,
            record_histogram_value(0, "histogram", envoy_stats_notags, 99, MILLISECONDS));

  terminate_engine(0);
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

  envoy_logger logger{[](envoy_data data, void* context) -> void {
                        auto* test_context = static_cast<engine_test_context*>(context);
                        data.release(data.context);
                        if (!test_context->on_log.HasBeenNotified()) {
                          test_context->on_log.Notify();
                        }
                      } /* log */,
                      [](void* context) -> void {
                        auto* test_context = static_cast<engine_test_context*>(context);
                        test_context->on_logger_release.Notify();
                      } /* release */,
                      &test_context};

  init_engine(engine_cbs, logger);
  run_engine(0, MINIMAL_NOOP_CONFIG.c_str(), LEVEL_DEBUG.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_TRUE(test_context.on_log.WaitForNotificationWithTimeout(absl::Seconds(3)));

  terminate_engine(0);
  ASSERT_TRUE(test_context.on_logger_release.WaitForNotificationWithTimeout(absl::Seconds(3)));
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

} // namespace Envoy
