#include <sys/resource.h>

#include <atomic>

#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"

#include "source/common/common/assert.h"

#include "test/common/http/common.h"
#include "test/common/integration/test_server.h"
#include "test/common/mocks/common/mocks.h"
#include "test/mocks/thread/mocks.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/api/external.h"
#include "library/common/bridge//utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"

namespace Envoy {

using testing::_;
using testing::ByMove;
using testing::Return;

constexpr Logger::Logger::Levels LOG_LEVEL = Logger::Logger::Levels::debug;
constexpr int kDefaultTimeoutSec = 3 * TIMEOUT_FACTOR;

struct EngineTestContext {
  absl::Notification on_engine_running;
  absl::Notification on_exit;
  absl::Notification on_log;
  absl::Notification on_log_exit;
  absl::Notification on_event;
};

// RAII wrapper for the engine, ensuring that we properly shut down the engine. If the engine
// thread is not torn down, we end up with TSAN failures during shutdown due to a data race
// between the main thread and the engine thread both writing to the
// Envoy::Logger::current_log_context global.
struct TestEngine {
  TestEngine(std::unique_ptr<EngineCallbacks> callbacks, const Logger::Logger::Levels log_level) {
    engine_ = std::make_unique<InternalEngine>(std::move(callbacks), /*logger=*/nullptr,
                                               /*event_tracker=*/nullptr);
    Platform::EngineBuilder builder;
    auto bootstrap = builder.generateBootstrap();
    auto options = std::make_shared<Envoy::OptionsImplBase>();
    options->setConfigProto(std::move(bootstrap));
    options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level));
    engine_->run(std::move(options));
  }

  envoy_engine_t handle() const { return reinterpret_cast<envoy_engine_t>(engine_.get()); }

  envoy_status_t terminate() const { return engine_->terminate(); }

  [[nodiscard]] bool isTerminated() const { return engine_->isTerminated(); }

  std::unique_ptr<InternalEngine> engine_;
};

std::unique_ptr<EngineCallbacks> createDefaultEngineCallbacks(EngineTestContext& test_context) {
  std::unique_ptr<EngineCallbacks> engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running_ = [&] { test_context.on_engine_running.Notify(); };
  engine_callbacks->on_exit_ = [&] { test_context.on_exit.Notify(); };
  return engine_callbacks;
}

// Transform C map to C++ map.
[[maybe_unused]] static inline std::map<std::string, std::string> toMap(envoy_map map) {
  std::map<std::string, std::string> new_map;
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    envoy_map_entry header = map.entries[i];
    const auto key = Bridge::Utility::copyToString(header.key);
    const auto value = Bridge::Utility::copyToString(header.value);
    new_map.emplace(key, value);
  }

  release_envoy_map(map);
  return new_map;
}

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  Http::ResponseHeaderMapPtr transformed_headers = Http::ResponseHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        Http::LowerCaseString(Bridge::Utility::copyToString(headers.entries[i].key)),
        Bridge::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

class InternalEngineTest : public testing::Test {
public:
  void SetUp() override {
    helper_handle_ = test::SystemHelperPeer::replaceSystemHelper();
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
  }

  envoy_status_t runEngine(const std::unique_ptr<InternalEngine>& engine,
                           const Platform::EngineBuilder& builder,
                           const Logger::Logger::Levels log_level) {
    auto bootstrap = builder.generateBootstrap();
    auto options = std::make_shared<Envoy::OptionsImplBase>();
    options->setConfigProto(std::move(bootstrap));
    options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level));
    return engine->run(std::move(options));
  }

  Http::RequestHeaderMapPtr createLocalhostRequestHeaders(absl::string_view address) {
    auto headers = Http::Utility::createRequestHeaderMapPtr();
    headers->setScheme("http");
    headers->setMethod("GET");
    headers->setHost(address);
    headers->setPath("/");
    return headers;
  }

  std::unique_ptr<TestEngine> engine_;

private:
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
};

TEST_F(InternalEngineTest, EarlyExit) {
  EngineTestContext test_context{};
  engine_ = std::make_unique<TestEngine>(createDefaultEngineCallbacks(test_context), LOG_LEVEL);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  ASSERT_EQ(engine_->terminate(), ENVOY_SUCCESS);
  ASSERT_TRUE(engine_->isTerminated());
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine_->engine_->startStream(0, {}, false);

  engine_.reset();
}

TEST_F(InternalEngineTest, AccessEngineAfterInitialization) {
  EngineTestContext test_context{};
  engine_ = std::make_unique<TestEngine>(createDefaultEngineCallbacks(test_context), LOG_LEVEL);
  engine_->handle();
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification getClusterManagerInvoked;
  // Running engine functions should work because the engine is running
  EXPECT_THAT(engine_->engine_->dumpStats(), testing::HasSubstr("runtime.load_success: 1\n"));

  engine_->terminate();
  ASSERT_TRUE(engine_->isTerminated());

  // Now that the engine has been shut down, we no longer expect scheduling to work.
  EXPECT_EQ("", engine_->engine_->dumpStats());

  engine_.reset();
}

TEST_F(InternalEngineTest, RecordCounter) {
  EngineTestContext test_context{};
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);

  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  EXPECT_EQ(ENVOY_SUCCESS, engine->recordCounterInc("counter", envoy_stats_notags, 1));

  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, Logger) {
  EngineTestContext test_context{};
  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log_ = [&](Logger::Logger::Levels, const std::string&) {
    if (!test_context.on_log.HasBeenNotified()) {
      test_context.on_log.Notify();
    }
  };
  logger->on_exit_ = [&] { test_context.on_log_exit.Notify(); };
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), std::move(logger), /*event_tracker=*/nullptr);
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  ASSERT_TRUE(
      test_context.on_log.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));

  engine->terminate();
  engine.reset();
  ASSERT_TRUE(
      test_context.on_log_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersDefaultAPI) {
  EngineTestContext test_context{};

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  // A default event tracker is registered in external API registry.
  const auto registered_event_tracker = static_cast<std::unique_ptr<EnvoyEventTracker>*>(
      Api::External::retrieveApi(ENVOY_EVENT_TRACKER_API_NAME));
  EXPECT_TRUE(registered_event_tracker != nullptr && *registered_event_tracker == nullptr);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that no crash if the assertion fails when no real event
  // tracker is passed at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersAPI) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track_ = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("foo") && events.at("foo") == "bar") {
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  const auto registered_event_tracker = static_cast<std::unique_ptr<EnvoyEventTracker>*>(
      Api::External::retrieveApi(ENVOY_EVENT_TRACKER_API_NAME));
  EXPECT_TRUE(registered_event_tracker != nullptr && *registered_event_tracker != nullptr);

  (*registered_event_tracker)->on_track_({{"foo", "bar"}});

  ASSERT_TRUE(
      test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersAssertionFailureRecordAction) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track_ = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("name") && events.at("name") == "assertion") {
      EXPECT_EQ(events.at("location"), "foo_location");
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  ASSERT_TRUE(
      test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersEnvoyBugRecordAction) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track_ = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("name") && events.at("name") == "bug") {
      EXPECT_EQ(events.at("location"), "foo_location");
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));

  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));
  // Simulate an envoy bug by invoking an Envoy bug failure
  // record action.
  // Verify that an envoy event is emitted when an event tracker is passed
  // at engine's initialization time.
  Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly("foo_location");

  ASSERT_TRUE(
      test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, BasicStream) {
  TestServer test_server;
  test_server.start(TestServerType::HTTP1_WITHOUT_TLS);

  EngineTestContext test_context{};
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);

  envoy::extensions::filters::http::buffer::v3::Buffer buffer;
  buffer.mutable_max_request_bytes()->set_value(65000);
  ProtobufWkt::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer");
  std::string serialized_buffer;
  buffer.SerializeToString(&serialized_buffer);
  typed_config.set_value(serialized_buffer);

  Platform::EngineBuilder builder;
  builder.addNativeFilter("buffer", typed_config);
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_complete_notification;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool /* end_stream */,
                                     envoy_stream_intel) {
    EXPECT_EQ(headers.Status()->value().getStringView(), "200");
  };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    on_complete_notification.Notify();
  };

  envoy_stream_t stream = engine->initStream();

  engine->startStream(stream, std::move(stream_callbacks), false);

  engine->sendHeaders(stream, createLocalhostRequestHeaders(test_server.getAddress()), false);
  engine->sendData(stream, std::make_unique<Buffer::OwnedImpl>("request body"), false);
  engine->sendTrailers(stream, Http::Utility::createRequestTrailerMapPtr());

  ASSERT_TRUE(on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  test_server.shutdown();
  engine->terminate();

  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(InternalEngineTest, ResetStream) {
  EngineTestContext test_context{};
  // There is nothing functional about the config used to run the engine, as the created stream is
  // immediately reset.
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_cancel_notification;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    on_cancel_notification.Notify();
  };

  envoy_stream_t stream = engine->initStream();

  engine->startStream(stream, std::move(stream_callbacks), false);

  engine->cancelStream(stream);

  ASSERT_TRUE(on_cancel_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine->terminate();

  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(InternalEngineTest, RegisterPlatformApi) {
  EngineTestContext test_context{};
  // Using the minimal envoy mobile config that allows for running the engine.
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  uint64_t fake_api;
  Envoy::Api::External::registerApi("api", &fake_api);

  engine->terminate();

  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(InternalEngineTest, ResetConnectivityState) {
  EngineTestContext test_context{};
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  Platform::EngineBuilder builder;
  runEngine(engine, builder, LOG_LEVEL);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(
      absl::Seconds(kDefaultTimeoutSec)));

  ASSERT_EQ(ENVOY_SUCCESS, engine->resetConnectivityState());

  engine->terminate();
  ASSERT_TRUE(
      test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(kDefaultTimeoutSec)));
}

TEST_F(InternalEngineTest, ThreadCreationFailed) {
  EngineTestContext test_context{};
  auto thread_factory = std::make_unique<Thread::MockPosixThreadFactory>();
  EXPECT_CALL(*thread_factory, createThread(_, _, false)).WillOnce(Return(ByMove(nullptr)));
  std::unique_ptr<InternalEngine> engine(new InternalEngine(
      createDefaultEngineCallbacks(test_context), {}, {}, {}, std::move(thread_factory)));
  Platform::EngineBuilder builder;
  envoy_status_t status = runEngine(engine, builder, LOG_LEVEL);
  EXPECT_EQ(status, ENVOY_FAILURE);
  // Calling `terminate()` should not crash.
  EXPECT_EQ(engine->terminate(), ENVOY_FAILURE);
}

class ThreadPriorityInternalEngineTest : public InternalEngineTest {
protected:
  // Starts an InternalEngine with the given priority and runs a request so the engine thread
  // priority can be retrieved.
  // Returns the engine's main thread priority.
  int startEngineWithPriority(const int thread_priority) {
    TestServer test_server;
    test_server.start(TestServerType::HTTP1_WITHOUT_TLS);

    EngineTestContext test_context{};
    std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
        createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr,
        thread_priority);
    Platform::EngineBuilder builder;
    runEngine(engine, builder, LOG_LEVEL);

    struct CallbackContext {
      absl::Notification on_complete_notification;
      int thread_priority = 0;
    };

    CallbackContext context;
    EnvoyStreamCallbacks stream_callbacks;
    stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap&, bool /* end_stream */,
                                       envoy_stream_intel) {
      // Gets the thread priority, so we can check that it's the same thread priority we set.
      context.thread_priority = engine->threadFactory().currentThreadPriority();
    };
    stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
      context.on_complete_notification.Notify();
    };

    envoy_stream_t stream = engine->initStream();
    engine->startStream(stream, std::move(stream_callbacks), false);
    engine->sendHeaders(stream, createLocalhostRequestHeaders(test_server.getAddress()), false);
    engine->sendData(stream, std::make_unique<Buffer::OwnedImpl>("request body"), true);

    EXPECT_TRUE(context.on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));
    test_server.shutdown();
    engine->terminate();
    EXPECT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));

    return context.thread_priority;
  }
};

TEST_F(ThreadPriorityInternalEngineTest, SetThreadPriority) {
  const int expected_thread_priority = 10;
  const int actual_thread_priority = startEngineWithPriority(expected_thread_priority);
  EXPECT_EQ(actual_thread_priority, expected_thread_priority);
}

TEST_F(ThreadPriorityInternalEngineTest, SetOutOfRangeThreadPriority) {
  // 102 is outside the range of acceptable thread priorities on all platforms.
  const int expected_thread_priority = 102;
  const int actual_thread_priority = startEngineWithPriority(expected_thread_priority);
  // The `setpriority` system call doesn't define what happens when the thread priority is out of
  // range, and the behavior could be system dependent. On Linux, if the supplied priority value
  // is greater than 19, then the thread priority value that gets set is 19. But since the behavior
  // could be system dependent, we just verify that the thread priority that gets set is not the
  // out-of-range one that we initialized the engine with.
  EXPECT_NE(actual_thread_priority, expected_thread_priority);
}

} // namespace Envoy
