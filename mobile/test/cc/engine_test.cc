#include "source/common/common/assert.h"

#include "test/common/integration/engine_with_test_server.h"
#include "test/common/integration/test_server.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/engine_types.h"
#include "library/common/http/header_utility.h"

namespace Envoy {

TEST(EngineTest, SetLogger) {
  std::atomic<bool> logging_was_called{false};
  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log_ = [&](Logger::Logger::Levels, const std::string&) { logging_was_called = true; };

  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running_ = [&] { engine_running.Notify(); };
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::debug)
      .setLogger(std::move(logger))
      .setEngineCallbacks(std::move(engine_callbacks))
      .enforceTrustChainVerification(false);
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);
  engine_running.WaitForNotification();

  std::string actual_status_code;
  bool actual_end_stream = false;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool end_stream,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
    actual_end_stream = end_stream;
  };
  stream_callbacks.on_data_ = [&](const Buffer::Instance&, uint64_t /* length */, bool end_stream,
                                  envoy_stream_intel) { actual_end_stream = end_stream; };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                   envoy_final_stream_intel) { stream_complete.Notify(); };
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  auto stream_prototype = engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream = stream_prototype->start(std::move(stream_callbacks));

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, "200");
  EXPECT_TRUE(actual_end_stream);
  EXPECT_TRUE(logging_was_called.load());
}

TEST(EngineTest, SetEngineCallbacks) {
  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running_ = [&] { engine_running.Notify(); };
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::debug)
      .setEngineCallbacks(std::move(engine_callbacks))
      .enforceTrustChainVerification(false);
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);
  engine_running.WaitForNotification();

  std::string actual_status_code;
  bool actual_end_stream = false;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool end_stream,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
    actual_end_stream = end_stream;
  };
  stream_callbacks.on_data_ = [&](const Buffer::Instance&, uint64_t /* length */, bool end_stream,
                                  envoy_stream_intel) { actual_end_stream = end_stream; };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                   envoy_final_stream_intel) { stream_complete.Notify(); };
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  auto stream_prototype = engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream = stream_prototype->start(std::move(stream_callbacks));

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, "200");
  EXPECT_TRUE(actual_end_stream);
}

TEST(EngineTest, SetEventTracker) {
  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running_ = [&] { engine_running.Notify(); };

  absl::Notification on_track;
  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track_ = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("name") && events.at("name") == "assertion") {
      EXPECT_EQ(events.at("location"), "foo_location");
      on_track.Notify();
    }
  };
  absl::Notification on_track_exit;
  event_tracker->on_exit_ = [&] { on_track_exit.Notify(); };

  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::debug)
      .setEngineCallbacks(std::move(engine_callbacks))
      .setEventTracker(std::move(event_tracker))
      .enforceTrustChainVerification(false);
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);
  engine_running.WaitForNotification();

  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  EXPECT_TRUE(on_track.WaitForNotificationWithTimeout(absl::Seconds(3)));
  EXPECT_TRUE(on_track.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST(EngineTest, DontWaitForOnEngineRunning) {
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::debug).enforceTrustChainVerification(false);
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);

  std::string actual_status_code;
  bool actual_end_stream = false;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool end_stream,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
    actual_end_stream = end_stream;
  };
  stream_callbacks.on_data_ = [&](const Buffer::Instance&, uint64_t /* length */, bool end_stream,
                                  envoy_stream_intel) { actual_end_stream = end_stream; };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                   envoy_final_stream_intel) { stream_complete.Notify(); };
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  auto stream_prototype = engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream = stream_prototype->start(std::move(stream_callbacks));

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, "200");
  EXPECT_TRUE(actual_end_stream);
}

TEST(EngineTest, TerminateWithoutWaitingForOnEngineRunning) {
  absl::Notification engine_running;
  auto engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running_ = [&] { engine_running.Notify(); };

  Platform::EngineBuilder engine_builder;
  auto engine = engine_builder.setLogLevel(Logger::Logger::debug).build();

  engine->terminate();
}

} // namespace Envoy
