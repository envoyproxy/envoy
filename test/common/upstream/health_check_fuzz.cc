#include "test/common/upstream/health_check_fuzz.h"
#include "test/common/upstream/utility.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Upstream {

HealthCheckFuzz::HealthCheckFuzz() {

}

void HealthCheckFuzz::allocHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config) {
    health_checker_ = std::make_shared<TestHttpHealthCheckerImpl>(
        *cluster_, config, dispatcher_, runtime_, random_,
        HealthCheckEventLoggerPtr(event_logger_storage_.release()));
}

void HealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
    allocHealthCheckerFromProto(input.health_check_config());
    addCompletionCallback();
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    expectSessionCreate();
    expectStreamCreate(0);
    //EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
    //Note: don't need these expects everywhere, just release on asserts if no longer need to fuzz
    health_checker_->start();
    replay(input);
}

//NEW RESPOND FIELDS:
void HealthCheckFuzz::respondHeaders(test::fuzz::Headers headers, absl::string_view status) { //input arg: fuzz headers, should I use std::string or absl::string()
    //convert fuzz headers to something usable - Http::TestResponseHeaderMapImpl
     std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(headers, {}, {}));
    response_headers->setStatus(status); //If Fuzzer created a status header - replace it with validated status

    //TODO: Expect clauses based on whether status is within range, and also if degraded or not
    //This will be from precedence notes

    //TODO: 0 or 1 depending on whether first or second
    test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), true);

    /*if (response_headers->has("degraded")) { //Seg fault here on this has call
        if (response_headers->get_("degraded") == "1") {
            ENVOY_LOG_MISC(trace, "Replied that the host is degraded");
            EXPECT_EQ(Host::Health::Degraded, cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());
            return;
        }
    }*/

    //No clauses that represent the host not being healthy
    //ENVOY_LOG_MISC(trace, "Replied that the host is Healthy");
    //EXPECT_EQ(Host::Health::Healthy, cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());
}

void HealthCheckFuzz::streamCreate() {
    ENVOY_LOG_MISC(trace, "Created a new stream.");
    expectStreamCreate(0);
    //EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
    test_sessions_[0]->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::replay(test::common::upstream::HealthCheckTestCase input) { //call this with the
    for (int i = 0; i < input.http_actions().size(); ++i) {
        const auto& event = input.http_actions(i);
        ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
        switch (event.action_selector_case()) { //TODO: Once added implementations for tcp and gRPC, move this to a seperate method, handleHttp
            case test::common::upstream::HttpAction::kRespond: { //Respond
                respondHeaders(event.respond().headers(), event.respond().status());
                break;
            }
            case test::common::upstream::HttpAction::kStreamCreate: { //Expect Stream Create
                streamCreate();
                break;
            }
            //TODO: Expect degradation behavior, unless this is can be taken care of by respond api exposed in unit test class
            default : {
                break;
            }
        }
    }
}

} //namespace Upstream
} //namespace Envoy
