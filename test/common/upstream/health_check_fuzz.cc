#include "health_check_fuzz.h"
#include "test/common/upstream/utility.h"

namespace Envoy {

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
    return yaml;
}

void HealthCheckFuzz::initialize(test::common::upstream::HealthCheckTestCase input) {
    allocHealthChecker(constructYamlFromProtoInput(input));
    addCompletionCallback();
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    expectSessionCreate();
    expectStreamCreate(0);
    replay(input);
}

void HealthCheckFuzz::respond(test::common::upstream::Respond respondFields) {
    Upstream::HttpHealthCheckerImplTest::respond(0, respondFields.code(), respondFields.conn_close(), respondFields.proxy_close(), respondFields.body(), respondFields.trailers(), {}, respondFields.degraded()); //TODO: HEALTH CHECK CLUSTER
}

void HealthCheckFuzz::streamCreate() {
    expectStreamCreate(0);
}

void HealthCheckFuzz::replay(test::common::upstream::HealthCheckTestCase input) { //call this with the
    for (int i = 0; i < input.http_config().actions().size(); ++i) {
        const auto& event = input.http_config().actions(i);
        ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
        switch (event.action_selector_case()) { //TODO: Once added implementations for tcp and gRPC, move this to a seperate method, handleHttp
            case test::common::upstream::HttpAction::kRespond: { //Respond
                respond(event.respond());
                break;
            }
            case test::common::upstream::HttpAction::kStreamCreate: { //Expect Stream Create
                streamCreate();
                break;
            }
            //TODO: Expect degradation behavior
            default : {
                break;
            }
        }
    }
}
}