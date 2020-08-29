#include "health_check_fuzz.h"
#include "test/common/health_check_fuzz.pb.h"

namespace Envoy {

//EITHER HAVE THIS HERE OR IN INITALIZE CALL
HealthCheckFuzz::HealthCheckFuzz() {
    //use input to construct config
    std::string yaml = R"EOF(
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
    )EOF";

    //sprintf(sqlAnswers, "select key from answer WHERE key = %s LIMIT 5;", tmp);

    allocHealthChecker(yaml);
    addCompletionCallback();
}

std::string HealthCheckFuzz::constructYamlFromProtoInput(test::common::upstream::HealthCheckTestCase input) {
    std::string yaml;
    sprintf(yaml, R"EOF(
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
    )EOF")
}

void HealthCheckFuzz::initalize(test::common::upstream::HealthCheckTestCase input) {
    allocHealthChecker(constructYamlFromProtoInput(input));
    addCompletionCallback();
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    expectSessionCreate();
    expectStreamCreate(0);
}

/**
Calls back into onHostStatus() after recieving a changed state
*/

/**
What I think are actions:
makeTestHost (this maybe one)
expectSessionCreate(); , also hits expectClientCreate(), this creates a new test session, and creates a new client
expectStreamCreate(0);
//This respond method basically hard codes the response vector headers, hardcodes body and trailers
respond(vars...) //can put the host into different states: Unhealthy, Degraded, Healthy, Unhealthy: not able to serve traffic, Degraded: able to serve traffic, but hosts that aren't degraded should be preffered. Healthy: Host is healthy and is able to serve traffic
.start(), but I feel like this should happen automatically
**/
/**
I feel like I could have the action response actually encapsulate the response it will send back with all the possible options exposed in the api.
*/
/**
Questions for Asra: I will have my proto have the exact same constraints? (talk about 3 seperate messages with the logic for each (one of), then the shared all in the config)
Iterated proto for hardcoded response, however body and trailers never have anything. I feel like it should have valid options for body and data that the upstream could send that would make it go through codec
*/

void HealthCheckFuzz::respond(test::common::upstream::HealthCheckTestCase respond) {
    respond(0, respond.code(), respond.conn_close(), respond.proxy_close(), respond.body(), respond.traliers(), respond.degraded()); //TODO: HEALTH CHECK CLUSTER
}

void HealthCheckFuzz::streamCreate() {
    expectStreamCreate(0);
}

HealthCheckFuzz::replay() { //call this with the
    for (int i = 0; i < input.actions.size(); ++i) {
        const auto& event = input.actions(i);
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