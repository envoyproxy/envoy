#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckFuzz::allocHttpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  HttpHealthCheckerImplTestBase::health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *HttpHealthCheckerImplTestBase::cluster_, config, HttpHealthCheckerImplTestBase::dispatcher_,
      HttpHealthCheckerImplTestBase::runtime_, HttpHealthCheckerImplTestBase::random_,
      HealthCheckEventLoggerPtr(HttpHealthCheckerImplTestBase::event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Http Health Checker");
}

void HealthCheckFuzz::allocTcpHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  TcpHealthCheckerImplTestBase::health_checker_ = std::make_shared<TcpHealthCheckerImpl>(
      *TcpHealthCheckerImplTestBase::cluster_, config, TcpHealthCheckerImplTestBase::dispatcher_,
      TcpHealthCheckerImplTestBase::runtime_, TcpHealthCheckerImplTestBase::random_,
      HealthCheckEventLoggerPtr(TcpHealthCheckerImplTestBase::event_logger_storage_.release()));
    ENVOY_LOG_MISC(trace, "Created Test Tcp Health Checker");
}

void HealthCheckFuzz::initializeAndReplay(
    test::common::upstream::HealthCheckTestCase input) { // Also Splits the two
  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: {
    type_ = HealthCheckFuzz::Type::HTTP;
    initializeAndReplayHttp(input);
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    type_ = HealthCheckFuzz::Type::TCP;
    initializeAndReplayTcp(input);
    break;
  }
  default:
    break;
  }
}

void HealthCheckFuzz::initializeAndReplayHttp(test::common::upstream::HealthCheckTestCase input) {
  allocHttpHealthCheckerFromProto(input.health_check_config());
  ON_CALL(HttpHealthCheckerImplTestBase::runtime_.snapshot_,
          featureEnabled("health_check.verify_cluster", 100))
      .WillByDefault(testing::Return(input.http_verify_cluster()));
  HttpHealthCheckerImplTestBase::cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(HttpHealthCheckerImplTestBase::cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  if (input.create_second_host()) {
    HttpHealthCheckerImplTestBase::cluster_->prioritySet().getMockHostSet(0)->hosts_.push_back(
        makeTestHost(HttpHealthCheckerImplTestBase::cluster_->info_, "tcp://127.0.0.1:81"));
    ENVOY_LOG_MISC(trace, "Created second host.");
    second_host_ = true;
    expectSessionCreate();
    expectStreamCreate(1);
  }
  HttpHealthCheckerImplTestBase::health_checker_->start();
  ON_CALL(HttpHealthCheckerImplTestBase::runtime_.snapshot_,
          getInteger("health_check.min_interval", _))
      .WillByDefault(testing::Return(45000));
  replay(input);
}

void HealthCheckFuzz::initializeAndReplayTcp(test::common::upstream::HealthCheckTestCase input) {
  allocTcpHealthCheckerFromProto(input.health_check_config());
  TcpHealthCheckerImplTestBase::cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(TcpHealthCheckerImplTestBase::cluster_->info_,
                   "tcp://127.0.0.1:80")}; // TODO: Support multiple hosts?
  TcpHealthCheckerImplTestBase::expectClientCreate();

  // connection_->....
  TcpHealthCheckerImplTestBase::health_checker_->start();
  TcpHealthCheckerImplTestBase::connection_->raiseEvent(Network::ConnectionEvent::Connected);

  replay(input);
}

void HealthCheckFuzz::respondHttp(test::fuzz::Headers headers, absl::string_view status,
                                  bool second_host) {
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(headers, {}, {}));

  response_headers->setStatus(status);

  const int index = (second_host_ && second_host) ? 1 : 0;

  test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                   true);
}

/*void HealthCheckFuzz::respondTcp(string ) {
  read_filter_->onData(, false);
}*/

void HealthCheckFuzz::streamCreate(bool second_host) {
  const int index = (second_host_ && second_host) ? 1 : 0;
  ENVOY_LOG_MISC(trace, "Created a new stream on host {}", index);
  expectStreamCreate(index);
  test_sessions_[index]->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::clientCreate() { TcpHealthCheckerImplTestBase::expectClientCreate(); }

void HealthCheckFuzz::raiseEvent(test::common::upstream::RaiseEvent event, bool second_host) {
  Network::ConnectionEvent eventType;
  switch (event.event_selector_case()) {
  case test::common::upstream::RaiseEvent::kConnected: {
    eventType = Network::ConnectionEvent::Connected;
    break;
  }
  case test::common::upstream::RaiseEvent::kRemoteClose: {
    eventType = Network::ConnectionEvent::RemoteClose;
    break;
  }
  case test::common::upstream::RaiseEvent::kLocalClose: {
    eventType = Network::ConnectionEvent::LocalClose;
    break;
  }
  default:
    break;
  }
  const int index = (second_host_ && second_host) ? 1 : 0;
  switch (type_) {
  case HealthCheckFuzz::Type::HTTP: {
    test_sessions_[index]->client_connection_->raiseEvent(eventType);
  }
  case HealthCheckFuzz::Type::TCP: {
    connection_->raiseEvent(eventType);
  }
  default:
    break;
  }
}

void HealthCheckFuzz::replay(test::common::upstream::HealthCheckTestCase input) {
  for (int i = 0; i < input.actions().size(); ++i) {
    const auto& event = input.actions(i);
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) { // TODO: Once added implementations for tcp and gRPC,
                                            // move this to a separate method, handleHttp
    case test::common::upstream::Action::kRespond: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        respondHttp(event.respond().http_respond().headers(),
                    event.respond().http_respond().status(), event.respond().second_host());
        break;
      }
      // TODO: TCP and gRPC
      case HealthCheckFuzz::Type::TCP: {
        ENVOY_LOG_MISC(trace, "Responded with {}. Length (in bytes) = {}.", event.respond().tcp_respond().data(), event.respond().tcp_respond().data().length());
        Buffer::OwnedImpl response;
        response.add(&event.respond().tcp_respond().data(), event.respond().tcp_respond().data().length());
        read_filter_->onData(response, false);
        break;
      }
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kStreamCreate: { // TODO: Map to client create for TCP
                                                          // across a switch
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        streamCreate(event.stream_create().second_host());
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        clientCreate();
        break;
      }
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kRaiseEvent: {
      raiseEvent(event.raise_event(), event.raise_event().second_host());
      break;
    }
    case test::common::upstream::Action::kAdvanceTime: {
      switch (type_) {
      case HealthCheckFuzz::Type::HTTP: {
        time_system_.advanceTimeAsync(
            std::chrono::milliseconds(event.advance_time().ms_advanced()));
        HttpHealthCheckerImplTestBase::dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
        break;
      }
      case HealthCheckFuzz::Type::TCP: {
        //TcpHealthCheckerImplTestBase::time_system_.advanceTimeAsync(
        //    std::chrono::milliseconds(event.advance_time().ms_advanced()));
        //TcpHealthCheckerImplTestBase::dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
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
