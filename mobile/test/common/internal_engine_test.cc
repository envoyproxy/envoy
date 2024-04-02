#include <sys/resource.h>

#include <atomic>

#include "source/common/common/assert.h"

#include "test/common/http/common.h"
#include "test/common/mocks/common/mocks.h"
#include "test/mocks/thread/mocks.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"

namespace Envoy {

using testing::_;
using testing::ByMove;
using testing::Return;

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
  TestEngine(std::unique_ptr<EngineCallbacks> callbacks, const std::string& level) {
    engine_.reset(new Envoy::InternalEngine(std::move(callbacks), {}, {}));
    Platform::EngineBuilder builder;
    auto bootstrap = builder.generateBootstrap();
    std::string yaml = Envoy::MessageUtil::getYamlStringFromMessage(*bootstrap);
    engine_->run(yaml, level);
  }

  envoy_engine_t handle() const { return reinterpret_cast<envoy_engine_t>(engine_.get()); }

  envoy_status_t terminate() const { return engine_->terminate(); }

  [[nodiscard]] bool isTerminated() const { return engine_->isTerminated(); }

  std::unique_ptr<InternalEngine> engine_;
};

std::unique_ptr<EngineCallbacks> createDefaultEngineCallbacks(EngineTestContext& test_context) {
  std::unique_ptr<EngineCallbacks> engine_callbacks = std::make_unique<EngineCallbacks>();
  engine_callbacks->on_engine_running = [&] { test_context.on_engine_running.Notify(); };
  engine_callbacks->on_exit = [&] { test_context.on_exit.Notify(); };
  return engine_callbacks;
}

// Transform C map to C++ map.
[[maybe_unused]] static inline std::map<std::string, std::string> toMap(envoy_map map) {
  std::map<std::string, std::string> new_map;
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    envoy_map_entry header = map.entries[i];
    const auto key = Data::Utility::copyToString(header.key);
    const auto value = Data::Utility::copyToString(header.value);
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
        Http::LowerCaseString(Data::Utility::copyToString(headers.entries[i].key)),
        Data::Utility::copyToString(headers.entries[i].value));
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

  std::unique_ptr<TestEngine> engine_;

private:
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
};

TEST_F(InternalEngineTest, EarlyExit) {
  EngineTestContext test_context{};
  engine_ = std::make_unique<TestEngine>(createDefaultEngineCallbacks(test_context), LEVEL_DEBUG);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  ASSERT_EQ(engine_->terminate(), ENVOY_SUCCESS);
  ASSERT_TRUE(engine_->isTerminated());
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine_->engine_->startStream(0, {}, false);

  engine_.reset();
}

TEST_F(InternalEngineTest, AccessEngineAfterInitialization) {
  EngineTestContext test_context{};
  engine_ = std::make_unique<TestEngine>(createDefaultEngineCallbacks(test_context), LEVEL_DEBUG);
  engine_->handle();
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification getClusterManagerInvoked;
  // Running engine functions should work because the engine is running
  EXPECT_EQ("runtime.load_success: 1\n", engine_->engine_->dumpStats());

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

  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  EXPECT_EQ(ENVOY_SUCCESS, engine->recordCounterInc("counter", envoy_stats_notags, 1));

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(InternalEngineTest, Logger) {
  EngineTestContext test_context{};
  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log = [&](Logger::Logger::Levels, const std::string&) {
    if (!test_context.on_log.HasBeenNotified()) {
      test_context.on_log.Notify();
    }
  };
  logger->on_exit = [&] { test_context.on_log_exit.Notify(); };
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), std::move(logger), /*event_tracker=*/nullptr);
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_TRUE(test_context.on_log.WaitForNotificationWithTimeout(absl::Seconds(3)));

  engine->terminate();
  engine.reset();
  ASSERT_TRUE(test_context.on_log_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersDefaultAPI) {
  EngineTestContext test_context{};

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

  // A default event tracker is registered in external API registry.
  const auto registered_event_tracker = static_cast<std::unique_ptr<EnvoyEventTracker>*>(
      Api::External::retrieveApi(ENVOY_EVENT_TRACKER_API_NAME));
  EXPECT_TRUE(registered_event_tracker != nullptr && *registered_event_tracker == nullptr);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  // Simulate a failed assertion by invoking a debug assertion failure
  // record action.
  // Verify that no crash if the assertion fails when no real event
  // tracker is passed at engine's initialization time.
  Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly("foo_location");

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersAPI) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("foo") && events.at("foo") == "bar") {
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));
  const auto registered_event_tracker = static_cast<std::unique_ptr<EnvoyEventTracker>*>(
      Api::External::retrieveApi(ENVOY_EVENT_TRACKER_API_NAME));
  EXPECT_TRUE(registered_event_tracker != nullptr && *registered_event_tracker != nullptr);

  (*registered_event_tracker)->on_track({{"foo", "bar"}});

  ASSERT_TRUE(test_context.on_event.WaitForNotificationWithTimeout(absl::Seconds(3)));
  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(InternalEngineTest, EventTrackerRegistersAssertionFailureRecordAction) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("name") && events.at("name") == "assertion") {
      EXPECT_EQ(events.at("location"), "foo_location");
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

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

TEST_F(InternalEngineTest, EventTrackerRegistersEnvoyBugRecordAction) {
  EngineTestContext test_context{};

  auto event_tracker = std::make_unique<EnvoyEventTracker>();
  event_tracker->on_track = [&](const absl::flat_hash_map<std::string, std::string>& events) {
    if (events.count("name") && events.at("name") == "bug") {
      EXPECT_EQ(events.at("location"), "foo_location");
      test_context.on_event.Notify();
    }
  };

  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, std::move(event_tracker));
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

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

TEST_F(InternalEngineTest, BasicStream) {
  EngineTestContext test_context{};
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  engine->run(BUFFERED_TEST_CONFIG, LEVEL_DEBUG);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_complete_notification;
  envoy_http_callbacks stream_cbs{
      [](envoy_headers c_headers, bool end_stream, envoy_stream_intel, void*) -> void {
        auto response_headers = toResponseHeaders(c_headers);
        EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
        EXPECT_TRUE(end_stream);
      } /* on_headers */,
      nullptr /* on_data */,
      nullptr /* on_metadata */,
      nullptr /* on_trailers */,
      nullptr /* on_error */,
      [](envoy_stream_intel, envoy_final_stream_intel, void* context) -> void {
        auto* on_complete_notification = static_cast<absl::Notification*>(context);
        on_complete_notification->Notify();
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

  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));
}

TEST_F(InternalEngineTest, ResetStream) {
  EngineTestContext test_context{};
  // There is nothing functional about the config used to run the engine, as the created stream is
  // immediately reset.
  std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
      createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr);
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification on_cancel_notification;
  envoy_http_callbacks stream_cbs{
      nullptr /* on_headers */,
      nullptr /* on_data */,
      nullptr /* on_metadata */,
      nullptr /* on_trailers */,
      nullptr /* on_error */,
      nullptr /* on_complete */,
      [](envoy_stream_intel, envoy_final_stream_intel, void* context) -> void {
        auto* on_cancel_notification = static_cast<absl::Notification*>(context);
        on_cancel_notification->Notify();
      } /* on_cancel */,
      nullptr /* on_send_window_available */,
      &on_cancel_notification /* context */};

  envoy_stream_t stream = engine->initStream();

  engine->startStream(stream, stream_cbs, false);

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
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

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
  engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  ASSERT_EQ(ENVOY_SUCCESS, engine->resetConnectivityState());

  engine->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));
}

TEST_F(InternalEngineTest, ThreadCreationFailed) {
  EngineTestContext test_context{};
  auto thread_factory = std::make_unique<Thread::MockPosixThreadFactory>();
  EXPECT_CALL(*thread_factory, createThread(_, _, false)).WillOnce(Return(ByMove(nullptr)));
  std::unique_ptr<InternalEngine> engine(new InternalEngine(
      createDefaultEngineCallbacks(test_context), {}, {}, {}, std::move(thread_factory)));
  envoy_status_t status = engine->run(BUFFERED_TEST_CONFIG, LEVEL_DEBUG);
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
    EngineTestContext test_context{};
    std::unique_ptr<InternalEngine> engine = std::make_unique<InternalEngine>(
        createDefaultEngineCallbacks(test_context), /*logger=*/nullptr, /*event_tracker=*/nullptr,
        thread_priority);
    engine->run(MINIMAL_TEST_CONFIG, LEVEL_DEBUG);

    struct CallbackContext {
      absl::Notification on_complete_notification;
      int thread_priority = 0;
    };

    CallbackContext context;
    envoy_http_callbacks stream_cbs{
        [](envoy_headers c_headers, bool, envoy_stream_intel, void* context) -> void {
          release_envoy_map(c_headers);
          // Gets the thread priority, so we can check that it's the same thread priority we set.
          auto* callback_context = static_cast<CallbackContext*>(context);
          callback_context->thread_priority = getpriority(PRIO_PROCESS, 0);
        } /* on_headers */,
        nullptr /* on_data */,
        nullptr /* on_metadata */,
        nullptr /* on_trailers */,
        nullptr /* on_error */,
        [](envoy_stream_intel, envoy_final_stream_intel, void* context) -> void {
          auto* callback_context = static_cast<CallbackContext*>(context);
          callback_context->on_complete_notification.Notify();
        } /* on_complete */,
        nullptr /* on_cancel */,
        nullptr /* on_send_window_available*/,
        &context};

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

    Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
    envoy_data c_data = Data::Utility::toBridgeData(request_data);

    envoy_stream_t stream = engine->initStream();
    engine->startStream(stream, stream_cbs, false);
    engine->sendHeaders(stream, c_headers, false);
    engine->sendData(stream, c_data, true);

    EXPECT_TRUE(context.on_complete_notification.WaitForNotificationWithTimeout(absl::Seconds(10)));
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
  // 42 is outside the range of acceptable thread priorities.
  const int expected_thread_priority = 42;
  const int actual_thread_priority = startEngineWithPriority(expected_thread_priority);
  // The `setpriority` system call doesn't define what happens when the thread priority is out of
  // range, and the behavior could be system dependent. On Linux, if the supplied priority value
  // is greater than 19, then the thread priority value that gets set is 19. But since the behavior
  // could be system dependent, we just verify that the thread priority that gets set is not the
  // out-of-range one that we initialiazed the engine with.
  EXPECT_NE(actual_thread_priority, expected_thread_priority);
}

} // namespace Envoy
