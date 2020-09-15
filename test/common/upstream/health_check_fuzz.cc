#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckFuzz::allocHttpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  http_test_base_->health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *http_test_base_->cluster_, config, http_test_base_->dispatcher_, http_test_base_->runtime_, http_test_base_->random_,
      HealthCheckEventLoggerPtr(http_test_base_->event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Http Health Checker");
}

void HealthCheckFuzz::allocTcpHealthCheckerFromProto(
  const envoy::config::core::v3::HealthCheck& config) {
    tcp_test_base_->health_checker_ = std::make_shared<TcpHealthCheckerImpl>(
      *tcp_test_base_->cluster_, config, tcp_test_base_->dispatcher_, tcp_test_base_->runtime_, tcp_test_base_->random_,
      HealthCheckEventLoggerPtr(tcp_test_base_->event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Tcp Health Checker");
}

void HealthCheckFuzz::initializeAndReplay(test::common::upstream::HealthCheckTestCase input) {
  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: {
    type_ = HealthCheckFuzz::Type::HTTP;
    http_test_base_ = new HttpHealthCheckerImplTestBase;
    initializeAndReplayHttp(input);
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    type_ = HealthCheckFuzz::Type::TCP;
    tcp_test_base_ = new TcpHealthCheckerImplTestBase;
    initializeAndReplayTcp(input);
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
    delete(http_test_base_);
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
  if (input.health_check_config().initial_jitter().seconds() != 0 ||
      input.health_check_config().initial_jitter().nanos() >= 500000) {
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
    delete(tcp_test_base_);
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }
  tcp_test_base_->cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
    makeTestHost(tcp_test_base_->cluster_->info_, "tcp://127.0.0.1:80")
  };
  tcp_test_base_->expectSessionCreate();
  tcp_test_base_->expectClientCreate();
  tcp_test_base_->health_checker_->start();
  if (input.health_check_config().has_reuse_connection()) {
    reuse_connection_ = input.health_check_config().reuse_connection().value();
  }
  //TODO: Hardcode a raise event connection here?
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
  }

  // If client already will close from connection header, no need for this check.
  if (response_headers->ProxyConnection() && !client_will_close) {
    client_will_close =
        absl::EqualsIgnoreCase(response_headers->ProxyConnection()->value().getStringView(),
                               Http::Headers::get().ConnectionValues.Close);
  }

  ENVOY_LOG_MISC(trace, "Responded headers {}", response_headers);
  http_test_base_->test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), true);

  if (!reuse_connection_ || client_will_close) {
    ENVOY_LOG_MISC(trace, "Creating client and stream because shouldClose() is true");
    http_test_base_->expectClientCreate(0);
    http_test_base_->expectStreamCreate(0);
    http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
  }
}

void HealthCheckFuzz::respondTcp(std::string data) { //Add an argument here
  
  Buffer::OwnedImpl response;
  response.add(&data, data.length());

  ENVOY_LOG_MISC(trace, "Responded with {}. Length (in bytes) = {}. This is the string passed in.", data, data.length());
  ENVOY_LOG_MISC(trace, "Responded with {}. This is the buffer generated from the string.", response.toString());
  //onData(Buffer::Instance& data)
  tcp_test_base_->read_filter_->onData(response, true);

  if (!reuse_connection_) {
    tcp_test_base_->expectClientCreate();
    tcp_test_base_->interval_timer_->invokeCallback();
  }
}

void HealthCheckFuzz::triggerIntervalTimerHttp() {
  // Interval timer needs to be explicitly enabled, usually by decodeHeaders.
  if (!http_test_base_->test_sessions_[0]->interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  http_test_base_->expectStreamCreate(0);
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::triggerIntervalTimerTcp() {
  if (!tcp_test_base_->interval_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Interval timer is disabled. Skipping trigger interval timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered interval timer");
  tcp_test_base_->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::triggerTimeoutTimerHttp(bool last_action) {
  // Timeout timer needs to be explicitly enabled, usually by a call to onIntervalBase().
  if (!http_test_base_->test_sessions_[0]->timeout_timer_->enabled_) {
    ENVOY_LOG_MISC(trace, "Timeout timer is disabled. Skipping trigger timeout timer.");
    return;
  }
  ENVOY_LOG_MISC(trace, "Triggered timeout timer");
  http_test_base_->test_sessions_[0]->timeout_timer_->invokeCallback(); // This closes the client, turns off timeout
                                                       // and enables interval
  if (!last_action) {
    ENVOY_LOG_MISC(trace, "Creating client and stream from network timeout");
    http_test_base_->expectClientCreate(0);
    http_test_base_->expectStreamCreate(0);
    http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
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
  case HealthCheckFuzz::Type::HTTP: {
    http_test_base_->test_sessions_[0]->client_connection_->raiseEvent(eventType);
    if (!last_action && eventType != Network::ConnectionEvent::Connected) {
      ENVOY_LOG_MISC(trace, "Creating client and stream from close event");
      http_test_base_->expectClientCreate(0);
      http_test_base_->expectStreamCreate(0);
      http_test_base_->test_sessions_[0]->interval_timer_->invokeCallback();
    }
    break;
  }
  case HealthCheckFuzz::Type::TCP: {
    tcp_test_base_->connection_->raiseEvent(eventType);
    if (!last_action && eventType != Network::ConnectionEvent::Connected) {
      ENVOY_LOG_MISC(trace, "Creating client from close event");
      tcp_test_base_->expectClientCreate();
      tcp_test_base_->interval_timer_->invokeCallback();
    }
    break;
  }
  default:
    break;
  }
}

void HealthCheckFuzz::replay(const test::common::upstream::HealthCheckTestCase& input) {
  for (int i = 0; i < input.actions().size(); ++i) {
    const auto& event = input.actions(i);
    const bool last_action = i == input.actions().size() - 1;
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) { // TODO: Once added implementations for tcp and gRPC,
                                            // move this to a separate method, handleHttp
    case test::common::upstream::Action::kRespond: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        // TODO: Hardcoded check on status Required because can't find documentation about required
        // validations for strings in protoc-gen-validate.
        if (event.respond().http_respond().status().empty()) {
          return;
        }
        respondHttp(event.respond().http_respond().headers(),
                    event.respond().http_respond().status());
        break;
      }
      // TODO: TCP and gRPC
      case HealthCheckFuzz::Type::TCP: {
        respondTcp(event.respond().tcp_respond().data());
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
        triggerIntervalTimerHttp();
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        triggerIntervalTimerTcp();
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
    //Shared method across all three
    case test::common::upstream::Action::kRaiseEvent: {
      raiseEvent(event.raise_event(), last_action);
      break;
    }
    default:
      break;
    }
  }
  // TODO: Cleanup?
  switch (type_) {
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
  }
}

} // namespace Upstream
} // namespace Envoy
