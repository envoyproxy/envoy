

namespace Envoy {

//EITHER HAVE THIS HERE OR IN INITALIZE CALL
HealthCheckFuzz::HealthCheckFuzz(test::common::upstream::HealthCheckTestCase input) {
    //use input to construct config
    std::string yaml = R"EOF(
        timeout: X
        interval: X
        interval_jitter: X
        unhealthy_threshold: X
        healthy_threshold": X
        http_health_check: //this is from the http message protobuf, specific to only http checking
            path: XSPECIFIC
            host: XSPECIFIC
            request_headers_to_add: ETC
        ETC (other common fields)
    )EOF";


    allocHealthChecker(yaml);
    addCompletionCallback();
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

void HealthCheckFuzz::respond() {

}

HealthCheckFuzz::replay() { //call this with the
    for (int i = 0; i < input.actions.size(); ++i) {
        const auto& event = input.actions(i);
        ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
        switch (event.action_selector_case()) {
            case : { //Respond
                event.respond().code();
                event.respond().conn_close();
                event.respond().proxy_close();
                //body
                //trailers
                //service cluster
                //degraded
                break;
            }
            case : {
                break;
            }
        }
    }
}
}