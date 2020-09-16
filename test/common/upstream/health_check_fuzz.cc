#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckFuzz::allocHttpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  http_test_base_->health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *http_test_base_->cluster_, config, http_test_base_->dispatcher_, http_test_base_->runtime_,
      http_test_base_->random_,
      HealthCheckEventLoggerPtr(http_test_base_->event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Http Health Checker");
}

void HealthCheckFuzz::allocTcpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  tcp_test_base_->health_checker_ = std::make_shared<TcpHealthCheckerImpl>(
      *tcp_test_base_->cluster_, config, tcp_test_base_->dispatcher_, tcp_test_base_->runtime_,
      tcp_test_base_->random_,
      HealthCheckEventLoggerPtr(tcp_test_base_->event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Tcp Health Checker");
}

void HealthCheckFuzz::initializeAndReplay(test::common::upstream::HealthCheckTestCase input) {
  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: { //TODO: Put delete here?
    type_ = HealthCheckFuzz::Type::HTTP;
    http_test_base_ = new HttpHealthCheckerImplTestBase;
    initializeAndReplayHttp(input);
    delete http_test_base_;
    ENVOY_LOG_MISC(trace, "Deleted http test base");
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    type_ = HealthCheckFuzz::Type::TCP;
    tcp_test_base_ = new TcpHealthCheckerImplTestBase;
    initializeAndReplayTcp(input);
    delete tcp_test_base_;
    ENVOY_LOG_MISC(trace, "Deleted tcp test base");
    break;
  }
  default:
    break;
  }
}

void HealthCheckFuzz::initializeAndReplayHttp(test::common::upstream::HealthCheckTestCase input) {
  try {
    allocHttpHealthCheckerFromProto(input.health_check_config());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }
  ON_CALL(http_test_base_->runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillByDefault(testing::Return(input.http_verify_cluster()));
  http_test_base_->cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(http_test_base_->cluster_->info_, "tcp://127.0.0.1:80")};
  http_test_base_->expectSessionCreate();
  http_test_base_->expectStreamCreate(0);
  // This sets up the possibility of testing hosts that never become healthy
  if (input.start_failed()) {
    http_test_base_->cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
        Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  http_test_base_->health_checker_->start();
  ON_CALL(http_test_base_->runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillByDefault(testing::Return(45000));

  // If has an initial jitter, this calls onIntervalBase and finishes startup
  if (DurationUtil::durationToMilliseconds(input.health_check_config().initial_jitter()) != 0) {
    http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
  }
  if (input.health_check_config().has_reuse_connection()) {
    reuse_connection_ = input.health_check_config().reuse_connection().value();
  }
  replay(input);
}

void HealthCheckFuzz::initializeAndReplayTcp(test::common::upstream::HealthCheckTestCase input) {
  try {
    allocTcpHealthCheckerFromProto(input.health_check_config());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }
  /*if (DurationUtil::durationToMilliseconds(input.health_check_config().initial_jitter()) != 0) {
      //delete tcp_test_base_;
      return;
      //tcp_test_base_->interval_timer_->invokeCallback(); //This calls timeout timer callback haha
  }*/
  tcp_test_base_->cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(tcp_test_base_->cluster_->info_, "tcp://127.0.0.1:80")};
  //tcp_test_base_->cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
  //    {tcp_test_base_->cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});
  tcp_test_base_->expectSessionCreate();
  tcp_test_base_->expectClientCreate();
  tcp_test_base_->health_checker_->start();
  ENVOY_LOG_MISC(trace, "Right after starting health checker interval enabled = {}", tcp_test_base_->interval_timer_->enabled_);
  ENVOY_LOG_MISC(trace, "Right after starting health checker timeout enabled = {}", tcp_test_base_->timeout_timer_->enabled_);
  if (input.health_check_config().has_reuse_connection()) {
    reuse_connection_ = input.health_check_config().reuse_connection().value();
  }

  //TODO: Get rid of this hardcoded check
  if (DurationUtil::durationToMilliseconds(input.health_check_config().initial_jitter()) != 0) {
    tcp_test_base_->interval_timer_->invokeCallback(); //This calls timeout timer callback haha
  }
  ENVOY_LOG_MISC(trace, "Before action loop interval enabled = {}", tcp_test_base_->interval_timer_->enabled_);
  ENVOY_LOG_MISC(trace, "Before action loop timeout enabled = {}", tcp_test_base_->timeout_timer_->enabled_);
  replay(input);
}

void HealthCheckFuzz::respondHttp(const test::fuzz::Headers& headers, absl::string_view status) {
  // Timeout timer needs to be explicitly enabled, usually by onIntervalBase() (Callback on interval
  // timer).
  if (!http_test_base_->test_sessions_[0]->timeout_timer_->enabled_) {
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
  http_test_base_->test_sessions_[0]->stream_response_callbacks_->decodeHeaders(
      std::move(response_headers), true);

  //Interval timer gets turned on from decodeHeaders()
  //TODO: What happens if respond respond before an interval timeframe finishes...that is a perfectly valid scenario
  if (!reuse_connection_ || client_will_close) {
    ENVOY_LOG_MISC(trace, "Creating client and stream because shouldClose() is true");
    triggerIntervalTimerHttp(true);
  }
}

void HealthCheckFuzz::respondTcp(std::string data, bool last_action) { // Add an argument here
  if (!tcp_test_base_->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping response.");
    return;
  }
  Buffer::OwnedImpl response;
  response.add(data);

  ENVOY_LOG_MISC(trace, "Responded with {}. Length (in bytes) = {}. This is the string passed in.",
                 data, data.length());
  // onData(Buffer::Instance& data)
  tcp_test_base_->read_filter_->onData(response, true);
  
  //The interval timer may not be on. If it's not on, return. An http response will automatically turn on interval and turn off timer,
  //but for tcp it doesnt if the data doesnt match. If it doesnt match, it only sets the host to unhealthy.
  //If it doesn't hit, it will leave timeout on and interval off.
  if (!reuse_connection_ && !last_action && tcp_test_base_->interval_timer_->enabled_) {
    tcp_test_base_->expectClientCreate();
    tcp_test_base_->interval_timer_->invokeCallback(); //Note: this will crash in asan mode if interval timer is disabled
  }
}

void HealthCheckFuzz::triggerIntervalTimerHttp(bool expect_client_create) {
  // Interval timer needs to be explicitly enabled, usually by decodeHeaders.
  if (!http_test_base_->test_sessions_[0]->interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  if (expect_client_create) {
    http_test_base_->expectClientCreate(0);
  }
  http_test_base_->expectStreamCreate(0);
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::triggerIntervalTimerTcp() {
  ENVOY_LOG_MISC(trace, "Trigger interval timer - interval enabled = {}", tcp_test_base_->interval_timer_->enabled_);
  ENVOY_LOG_MISC(trace, "Trigger interval timer - timeout enabled = {}", tcp_test_base_->timeout_timer_->enabled_);
  if (!tcp_test_base_->interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  tcp_test_base_->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::triggerTimeoutTimerHttp(bool last_action) {
  ENVOY_LOG_MISC(trace, "Trigger timeout timer - interval enabled = {}", tcp_test_base_->interval_timer_->enabled_);
  ENVOY_LOG_MISC(trace, "Trigger timeout timer - timeout enabled = {}", tcp_test_base_->timeout_timer_->enabled_);
  // Timeout timer needs to be explicitly enabled, usually by a call to onIntervalBase().
  if (!http_test_base_->test_sessions_[0]->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping trigger timeout timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered timeout timer");
  http_test_base_->test_sessions_[0]
      ->timeout_timer_->invokeCallback(); // This closes the client, turns off timeout
                                          // and enables interval
  if (!last_action) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from network timeout");
    triggerIntervalTimerHttp(true);
  }
}

void HealthCheckFuzz::triggerTimeoutTimerTcp(bool last_action) {
  if (!tcp_test_base_->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping trigger timeout timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered timeout timer");
  tcp_test_base_->timeout_timer_->invokeCallback(); // This closes the client, turns off timeout
                                                    // and enables interval
  if (!last_action) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from network timeout");
    tcp_test_base_->expectClientCreate();
    tcp_test_base_->interval_timer_->invokeCallback();
  }
}

void HealthCheckFuzz::raiseEvent(const test::common::upstream::RaiseEvent& event,
                                 bool last_action) {
  Network::ConnectionEvent eventType;
  switch (event) {
  case test::common::upstream::RaiseEvent::CONNECTED: {
    eventType = Network::ConnectionEvent::Connected;
    break;
  }
  case test::common::upstream::RaiseEvent::REMOTE_CLOSE: {
    eventType = Network::ConnectionEvent::RemoteClose;
    break;
  }
  case test::common::upstream::RaiseEvent::LOCAL_CLOSE: {
    eventType = Network::ConnectionEvent::LocalClose;
    break;
  }
  default: // shouldn't hit
    eventType = Network::ConnectionEvent::Connected;
    break;
  }

  switch (type_) {
  case HealthCheckFuzz::Type::HTTP: { //TODO: Do I need check for timeout timer enabled here?
    http_test_base_->test_sessions_[0]->client_connection_->raiseEvent(eventType);
    if (!last_action && eventType != Network::ConnectionEvent::Connected) {
      ENVOY_LOG_MISC(trace, "Creating client and stream from close event");
      triggerIntervalTimerHttp(true); //Interval timer is guaranteed to be on from raiseEvent not connected - calls onResetStream which handles failure, turning interval timer on and timeout off
    }
    break;
  }
  case HealthCheckFuzz::Type::TCP: {
    tcp_test_base_->connection_->raiseEvent(eventType);
    if (!last_action && eventType != Network::ConnectionEvent::Connected) {
      if (!tcp_test_base_->interval_timer_->enabled_) { //Note: related to the TODO below. This will mean that both timers are disabled, meaning any action after hitting this will be voided.
        return;
      }
      ENVOY_LOG_MISC(trace, "Creating client from close event");
      tcp_test_base_->expectClientCreate();
      //The interval timer may not be enabled from close event - gets set to enabled when called with (_, _), this calls it with a number
      //It calls it with a number if expect close is false (like 4 different code paths). TODO: Figure out what to do in this scenario.
      tcp_test_base_->interval_timer_->invokeCallback();
    }
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
    switch (event.action_selector_case()) { // TODO: Once added implementations for tcp and gRPC,
                                            // move this to a separate method, handleHttp
    case test::common::upstream::Action::kRespond: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        respondHttp(event.respond().http_respond().headers(),
                    event.respond().http_respond().status());
        break;
      }
      // TODO: TCP and gRPC
      case HealthCheckFuzz::Type::TCP: {
        respondTcp(event.respond().tcp_respond().data(), last_action);
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
        triggerIntervalTimerHttp(false);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        triggerIntervalTimerTcp();
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
        triggerTimeoutTimerHttp(last_action);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        triggerTimeoutTimerTcp(last_action);
        break;
      }
      default:
        break;
      }
      break;
    }
    // Shared method across all three
    case test::common::upstream::Action::kRaiseEvent: {
      raiseEvent(event.raise_event(), last_action);
      break;
    }
    default:
      break;
    }
  }
  // TODO: Cleanup?
  /*switch (type_) {
  case HealthCheckFuzz::Type::HTTP: {
    delete http_test_base_;
    ENVOY_LOG_MISC(trace, "Deleted http test base");
    break;
  }
  case HealthCheckFuzz::Type::TCP: {
    delete tcp_test_base_;
    ENVOY_LOG_MISC(trace, "Deleted tcp test base");
    break;
  }
  default:
    break;
  }*/
}

} // namespace Upstream
} // namespace Envoy
