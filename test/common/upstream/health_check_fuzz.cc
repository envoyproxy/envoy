#include "test/common/upstream/health_check_fuzz.h"

#include <chrono>

#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

HealthCheckFuzz::HealthCheckFuzz() {}

void HealthCheckFuzz::allocHealthCheckerFromProto(
    const envoy::config::core::v3::HealthCheck& config) {
  health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
      *cluster_, config, dispatcher_, runtime_, random_,
      HealthCheckEventLoggerPtr(event_logger_storage_.release()));
  ENVOY_LOG_MISC(trace, "Created Test Health Checker");
}

void HealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
  allocHealthCheckerFromProto(input.health_check_config());
  addCompletionCallback();
  if (input.second_host()) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
        Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};
  } else {
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  }
  expectSessionCreate();
  expectStreamCreate(0);
  if (input.second_host()) {
    ENVOY_LOG_MISC(trace, "Created second host.");
    second_host_ = true;
    expectSessionCreate();
    expectStreamCreate(1);
  }
  health_checker_->start();
  replay(input);
}

void HealthCheckFuzz::respondHeaders(
    test::fuzz::Headers headers, absl::string_view status,
    bool respond_on_second_host) {
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(headers, {}, {}));
  
  // If Fuzzer created a status header - replace it with validated status
  response_headers->setStatus(status);

  int index = (second_host_ && respond_on_second_host) ? 1 : 0;

  test_sessions_[index]->stream_response_callbacks_->decodeHeaders(
        std::move(response_headers), true);
}

void HealthCheckFuzz::streamCreate(bool create_stream_on_second_host) {
  ENVOY_LOG_MISC(trace, "Created a new stream on host 1.");
  if (second_host_) {
    int index = (create_stream_on_second_host) ? 1 : 0;
    expectStreamCreate(index);
    test_sessions_[index]->interval_timer_->invokeCallback();
  } else {
    expectStreamCreate(0);
    test_sessions_[0]->interval_timer_->invokeCallback();
  }
}

void HealthCheckFuzz::replay(
    test::common::upstream::HealthCheckTestCase input) {
  for (int i = 0; i < input.http_actions().size(); ++i) {
    const auto& event = input.http_actions(i);
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) { // TODO: Once added implementations for tcp and gRPC,
                                            // move this to a seperate method, handleHttp
    case test::common::upstream::HttpAction::kRespond: {
      respondHeaders(event.respond().headers(), event.respond().status(),
                     event.respond().respond_on_second_host());
      break;
    }
    case test::common::upstream::HttpAction::kStreamCreate: {
      streamCreate(event.stream_create().create_stream_on_second_host());
      break;
    }
    case test::common::upstream::HttpAction::kAdvanceTime: {
        time_system_.advanceTimeAsync(
            std::chrono::milliseconds(event.advance_time().ms_advanced())
        );
        dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
        break;
    }
    default: {
      break;
    }
    }
  }
}

} // namespace Upstream
} // namespace Envoy
