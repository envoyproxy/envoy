#include "test/common/upstream/health_check_fuzz.h"
#include "test/common/upstream/utility.h"

namespace Envoy {
namespace Upstream {

HealthCheckFuzz::HealthCheckFuzz() {

}

std::string HealthCheckFuzz::constructYamlFromProtoInput(test::common::upstream::HealthCheckTestCase input) {
    input.http_config();
    std::string yaml; //TODO: hardcode at first, then provide options
    /*sprintf(yaml, R"EOF(
        timeout: %us
        interval: %us
        initial_jitter: %us
        interval_jitter: %us
        interval_jitter_percent: %u
        unhealthy_threshold: %u
        healthy_threshold: %u
        reuse_connection: %s //x ? "true" : "false"
        no_traffic_interval: %us
        unhealthy_interval: %us
        unhealthy_edge_interval: %us
        healthy_edge_interval: %us
        always_log_health_check_failures: %s //x ? "true" : "false"
        http_health_check: //this is from the http message protobuf, specific to only http checking
            host: %s
            path: %s
            service_name: %s
    )EOF")*/
    //hardcoded first unit test
    yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name_matcher:
        prefix: locations
      path: /healthcheck
    )EOF";
    return yaml;
}

void HealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
    allocHealthChecker(constructYamlFromProtoInput(input));
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

void HealthCheckFuzz::respondFields(test::common::upstream::Respond respondFields) {
    respond(0, respondFields.code(), respondFields.conn_close(), respondFields.proxy_close(), respondFields.body(), respondFields.trailers(), {}, respondFields.degraded()); //TODO: HEALTH CHECK CLUSTER
    //Most precedence up here, return if hits
    //EXPECT BASED ON RESPONSE CODE, compared to respondfields
    if (respondFields.degraded()) {
        ENVOY_LOG_MISC(trace, "Replied that the host is degraded");
        EXPECT_EQ(Host::Health::Degraded, cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());
        return;
    }
    //No clauses that represent the host not being healthy
    ENVOY_LOG_MISC(trace, "Replied that the host is Healthy");
    EXPECT_EQ(Host::Health::Healthy, cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->health());
    return;
}

void HealthCheckFuzz::streamCreate() {
    ENVOY_LOG_MISC(trace, "Created a new stream.");
    expectStreamCreate(0);
    //EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
    test_sessions_[0]->interval_timer_->invokeCallback();
}

void HealthCheckFuzz::replay(test::common::upstream::HealthCheckTestCase input) { //call this with the
    for (int i = 0; i < input.http_config().actions().size(); ++i) {
        const auto& event = input.http_config().actions(i);
        ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
        switch (event.action_selector_case()) { //TODO: Once added implementations for tcp and gRPC, move this to a seperate method, handleHttp
            case test::common::upstream::HttpAction::kRespond: { //Respond
                respondFields(event.respond());
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