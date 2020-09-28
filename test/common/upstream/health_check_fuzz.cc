#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>
#include <memory>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

void HttpHealthCheckFuzz::allocHttpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *cluster_, config, dispatcher_, runtime_, random_,
      HealthCheckEventLoggerPtr(event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Http Health Checker");
}

void HttpHealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
  allocHttpHealthCheckerFromProto(input.health_check_config());
  ON_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillByDefault(testing::Return(input.http_verify_cluster()));
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  // This sets up the possibility of testing hosts that never become healthy
  if (input.start_failed()) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
        Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  health_checker_->start();
  ON_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillByDefault(testing::Return(45000));
  // If has an initial jitter, this calls onIntervalBase and finishes startup
  if (DurationUtil::durationToMilliseconds(input.health_check_config().initial_jitter()) != 0) {
    test_sessions_[0]->interval_timer_->invokeCallback();
  }
  if (input.health_check_config().has_reuse_connection()) {
    reuse_connection_ = input.health_check_config().reuse_connection().value();
  }
}

void HttpHealthCheckFuzz::respond(const test::fuzz::Headers& headers, uint64_t status) {
  // Timeout timer needs to be explicitly enabled, usually by onIntervalBase() (Callback on interval
  // timer).
  if (!test_sessions_[0]->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping response.");
    return;
  }

  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(headers, {}, {}));

  response_headers->setStatus(status);

  // Responding with http can cause client to close, if so create a new one.
  bool client_will_close = false;
  if (response_headers->Connection()) {
    client_will_close =
        absl::EqualsIgnoreCase(response_headers->Connection()->value().getStringView(),
                               Http::Headers::get().ConnectionValues.Close);
  } else if (response_headers->ProxyConnection()) {
    client_will_close =
        absl::EqualsIgnoreCase(response_headers->ProxyConnection()->value().getStringView(),
                               Http::Headers::get().ConnectionValues.Close);
  }

  ENVOY_LOG_MISC(trace, "Responded headers {}", *response_headers.get());
  test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), true);

  // Interval timer gets turned on from decodeHeaders()
  if (!reuse_connection_ || client_will_close) {
    ENVOY_LOG_MISC(trace, "Creating client and stream because shouldClose() is true");
    triggerIntervalTimer(true);
  }
}

void HttpHealthCheckFuzz::triggerIntervalTimer(bool expect_client_create) {
  // Interval timer needs to be explicitly enabled, usually by decodeHeaders.
  if (!test_sessions_[0]->interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  if (expect_client_create) {
    expectClientCreate(0);
  }
  expectStreamCreate(0);
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  test_sessions_[0]->interval_timer_->invokeCallback();
}

void HttpHealthCheckFuzz::triggerTimeoutTimer(bool last_action) {
  // Timeout timer needs to be explicitly enabled, usually by a call to onIntervalBase().
  if (!test_sessions_[0]->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping trigger timeout timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered timeout timer");
  test_sessions_[0]->timeout_timer_->invokeCallback(); // This closes the client, turns off timeout
                                                       // and enables interval
  if (!last_action) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from network timeout");
    triggerIntervalTimer(true);
  }
}

void HttpHealthCheckFuzz::raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) {
  test_sessions_[0]->client_connection_->raiseEvent(event_type);
  if (!last_action && event_type != Network::ConnectionEvent::Connected) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from close event");
    triggerIntervalTimer(
        true); // Interval timer is guaranteed to be enabled from a close event - calls
               // onResetStream which handles failure, turning interval timer on and timeout off
  }
}

void TcpHealthCheckFuzz::allocTcpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  health_checker_ = std::make_shared<TcpHealthCheckerImpl>(
      *cluster_, config, dispatcher_, runtime_, random_,
      HealthCheckEventLoggerPtr(event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Tcp Health Checker");
}

void TcpHealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
  allocTcpHealthCheckerFromProto(input.health_check_config());
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  health_checker_->start();
  if (input.health_check_config().has_reuse_connection()) {
    reuse_connection_ = input.health_check_config().reuse_connection().value();
  }
  // The Receive proto message has a validation that if there is a receive field, the text field, a
  // string representing the hex encoded payload has a least one byte.
  if (input.health_check_config().tcp_health_check().receive_size() != 0) {
    ENVOY_LOG_MISC(trace, "Health Checker is only testing to connect");
    empty_response_ = false;
  }
  // Clang tidy throws an error here in regards to a potential leak. It seems to have something to
  // do with shared_ptr and possible cycles in regards to the clusters host objects. Since all this
  // test class directly uses the unit test class that has been in master for a long time, this is
  // likely a false positive.
  if (DurationUtil::durationToMilliseconds(input.health_check_config().initial_jitter()) != 0) {
    interval_timer_->invokeCallback();
  }
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void TcpHealthCheckFuzz::respond(std::string data, bool last_action) {
  if (!timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping response.");
    return;
  }
  Buffer::OwnedImpl response;
  response.add(data);

  ENVOY_LOG_MISC(trace, "Responded with {}. Length (in bytes) = {}. This is the string passed in.",
                 data, data.length());
  read_filter_->onData(response, true);

  // The interval timer may not be on. If it's not on, return. An http response will automatically
  // turn on interval and turn off timeout, but for tcp it doesn't if the data doesn't match. If the
  // response doesn't match, it only sets the host to unhealthy. If it does match, it will turn
  // timeout off and interval on.
  if (!reuse_connection_ && !last_action && interval_timer_->enabled_) {
    expectClientCreate();
    interval_timer_->invokeCallback();
  }
}

void TcpHealthCheckFuzz::triggerIntervalTimer() {
  if (!interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  interval_timer_->invokeCallback();
}

void TcpHealthCheckFuzz::triggerTimeoutTimer(bool last_action) {
  if (!timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping trigger timeout timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered timeout timer");
  timeout_timer_->invokeCallback(); // This closes the client, turns off timeout
                                    // and enables interval
  if (!last_action) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from network timeout");
    expectClientCreate();
    interval_timer_->invokeCallback();
  }
}

void TcpHealthCheckFuzz::raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) {
  // On a close event, the health checker will call handleFailure if expect_close_ is false. This is
  // set by multiple code paths. handleFailure() turns on interval and turns off timeout. However,
  // other action of the fuzzer account for this by explicitly invoking a client after
  // expect_close_ gets set to true, turning expect_close_ back to false.
  connection_->raiseEvent(event_type);
  if (!last_action && event_type != Network::ConnectionEvent::Connected) {
    if (!interval_timer_->enabled_) {
      return;
    }
    ENVOY_LOG_MISC(trace, "Creating client from close event");
    expectClientCreate();
    interval_timer_->invokeCallback();
  }

  // In the specific case of:
  // https://github.com/envoyproxy/envoy/blob/master/source/common/upstream/health_checker_impl.cc#L489
  // This blows away client, should create a new one
  if (event_type == Network::ConnectionEvent::Connected && empty_response_) {
    ENVOY_LOG_MISC(trace, "Creating client from connected event and empty response.");
    expectClientCreate();
    interval_timer_->invokeCallback();
  }
}

Network::ConnectionEvent
HealthCheckFuzz::getEventTypeFromProto(const test::common::upstream::RaiseEvent& event) {
  switch (event) {
  case test::common::upstream::RaiseEvent::CONNECTED: {
    return Network::ConnectionEvent::Connected;
  }
  case test::common::upstream::RaiseEvent::REMOTE_CLOSE: {
    return Network::ConnectionEvent::RemoteClose;
  }
  case test::common::upstream::RaiseEvent::LOCAL_CLOSE: {
    return Network::ConnectionEvent::LocalClose;
  }
  default: // shouldn't hit
    NOT_REACHED_GCOVR_EXCL_LINE;
    break;
  }
}

void HealthCheckFuzz::initializeAndReplay(test::common::upstream::HealthCheckTestCase input) {
  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: {
    type_ = HealthCheckFuzz::Type::HTTP;
    http_fuzz_test_ = std::make_unique<HttpHealthCheckFuzz>();
    try { // Catches exceptions related to initializing health checker
      http_fuzz_test_->initialize(input);
    } catch (EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
      return;
    }
    replay(input);
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    type_ = HealthCheckFuzz::Type::TCP;
    tcp_fuzz_test_ = std::make_unique<TcpHealthCheckFuzz>();
    try { // Catches exceptions related to initializing health checker
      tcp_fuzz_test_->initialize(input);
    } catch (EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
      return;
    }
    replay(input);
    break;
  }
  default:
    break;
  }
}

void HealthCheckFuzz::replay(const test::common::upstream::HealthCheckTestCase& input) {
  constexpr auto max_actions = 64;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const auto& event = input.actions(i);
    const bool last_action = i == std::min(max_actions, input.actions().size()) - 1;
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) {
    case test::common::upstream::Action::kRespond: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        http_fuzz_test_->respond(event.respond().http_respond().headers(),
                                 event.respond().http_respond().status());
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        tcp_fuzz_test_->respond(event.respond().tcp_respond().data(), last_action);
        break;
      }
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kTriggerIntervalTimer: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        http_fuzz_test_->triggerIntervalTimer(false);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        tcp_fuzz_test_->triggerIntervalTimer();
        break;
      }
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kTriggerTimeoutTimer: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        http_fuzz_test_->triggerTimeoutTimer(last_action);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        tcp_fuzz_test_->triggerTimeoutTimer(last_action);
        break;
      }
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kRaiseEvent: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        http_fuzz_test_->raiseEvent(getEventTypeFromProto(event.raise_event()), last_action);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        tcp_fuzz_test_->raiseEvent(getEventTypeFromProto(event.raise_event()), last_action);
        break;
      }
      default:
        break;
      }
      break;
    }
    default:
      break;
    }
  }
}

} // namespace Upstream
} // namespace Envoy
