#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"

namespace Envoy {

TEST(EngineTest, SetLogger) {
  std::atomic logging_was_called{false};
  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log = [&](Logger::Logger::Levels, const std::string&) { logging_was_called = true; };

  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running = [&] { engine_running.Notify(); };
  Platform::EngineBuilder engine_builder;
  Platform::EngineSharedPtr engine =
      engine_builder.setLogLevel(Logger::Logger::debug)
          .setLogger(std::move(logger))
          .setEngineCallbacks(std::move(engine_callbacks))
          .addNativeFilter(
              "test_remote_response",
              "{'@type': "
              "type.googleapis.com/"
              "envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse}")
          .build();
  engine_running.WaitForNotification();

  int actual_status_code = 0;
  bool actual_end_stream = false;
  absl::Notification stream_complete;
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  auto stream = (*stream_prototype)
                    .setOnHeaders([&](Platform::ResponseHeadersSharedPtr headers, bool end_stream,
                                      envoy_stream_intel) {
                      actual_status_code = headers->httpStatus();
                      actual_end_stream = end_stream;
                    })
                    .setOnData([&](envoy_data data, bool end_stream) {
                      actual_end_stream = end_stream;
                      release_envoy_data(data);
                    })
                    .setOnComplete([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .setOnError([&](Platform::EnvoyErrorSharedPtr, envoy_stream_intel,
                                    envoy_final_stream_intel) { stream_complete.Notify(); })
                    .setOnCancel([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .start();

  auto request_headers =
      Platform::RequestHeadersBuilder(Platform::RequestMethod::GET, "https", "example.com", "/")
          .build();
  stream->sendHeaders(std::make_shared<Platform::RequestHeaders>(request_headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, 200);
  EXPECT_EQ(actual_end_stream, true);
  EXPECT_TRUE(logging_was_called.load());
  EXPECT_EQ(engine->terminate(), ENVOY_SUCCESS);
}

TEST(EngineTest, SetEngineCallbacks) {
  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running = [&] { engine_running.Notify(); };
  Platform::EngineBuilder engine_builder;
  Platform::EngineSharedPtr engine =
      engine_builder.setLogLevel(Logger::Logger::debug)
          .setEngineCallbacks(std::move(engine_callbacks))
          .addNativeFilter(
              "test_remote_response",
              "{'@type': "
              "type.googleapis.com/"
              "envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse}")
          .build();
  engine_running.WaitForNotification();

  int actual_status_code = 0;
  bool actual_end_stream = false;
  absl::Notification stream_complete;
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  auto stream = (*stream_prototype)
                    .setOnHeaders([&](Platform::ResponseHeadersSharedPtr headers, bool end_stream,
                                      envoy_stream_intel) {
                      actual_status_code = headers->httpStatus();
                      actual_end_stream = end_stream;
                    })
                    .setOnData([&](envoy_data data, bool end_stream) {
                      actual_end_stream = end_stream;
                      release_envoy_data(data);
                    })
                    .setOnComplete([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .setOnError([&](Platform::EnvoyErrorSharedPtr, envoy_stream_intel,
                                    envoy_final_stream_intel) { stream_complete.Notify(); })
                    .setOnCancel([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .start();

  auto request_headers =
      Platform::RequestHeadersBuilder(Platform::RequestMethod::GET, "https", "example.com", "/")
          .build();
  stream->sendHeaders(std::make_shared<Platform::RequestHeaders>(request_headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, 200);
  EXPECT_EQ(actual_end_stream, true);
  EXPECT_EQ(engine->terminate(), ENVOY_SUCCESS);
}

} // namespace Envoy
