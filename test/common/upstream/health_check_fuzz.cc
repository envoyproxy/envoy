#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckFuzz::allocHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *cluster_, config, dispatcher_, runtime_, random_,
      HealthCheckEventLoggerPtr(event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Health Checker");
}

void HealthCheckFuzz::initializeAndReplay(test::common::upstream::HealthCheckTestCase input) {
  second_host_ = false;
  allocHealthCheckerFromProto(input.health_check_config());
  ON_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillByDefault(testing::Return(input.http_verify_cluster()));
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  if (input.create_second_host()) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_.push_back(
        makeTestHost(cluster_->info_, "tcp://127.0.0.1:81"));
    ENVOY_LOG_MISC(trace, "Created second host.");
    second_host_ = true;
    expectSessionCreate();
    expectStreamCreate(1);
  }
  health_checker_->start();
  ON_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillByDefault(testing::Return(45000));
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

void HealthCheckFuzz::streamCreate(bool second_host) {
  const int index = (second_host_ && second_host) ? 1 : 0;
  ENVOY_LOG_MISC(trace, "Created a new stream on host {}", index);
  expectStreamCreate(index);
  test_sessions_[index]->interval_timer_->invokeCallback();
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
      default:
        break;
      }
      break;
    }
    case test::common::upstream::Action::kStreamCreate: {
      streamCreate(event.stream_create().second_host());
      break;
    }
    case test::common::upstream::Action::kAdvanceTime: {
      time_system_.advanceTimeAsync(std::chrono::milliseconds(event.advance_time().ms_advanced()));
      dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
      break;
    }
    default:
      break;
    }
  }
}

} // namespace Upstream
} // namespace Envoy
