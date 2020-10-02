//Mod it across host sets to determine which one to use


#include "test/common/upstream/load_balancer_fuzz.h"
#include "test/common/upstream/load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Upstream {



DEFINE_PROTO_FUZZER(const test::common::upstream::LoadBalancerTestCase input) {
    try {
        TestUtility::validate(input);
    } catch (const ProtoValidationException& e) {
        ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
        return;
    }
    LoadBalancerFuzzBase load_balancer_fuzz;

    //TODO: Switch across type, etc.?
    load_balancer_fuzz = RandomLoadBalancerFuzzTest;

    load_balancer_fuzz.initializeAndReplay(input);
}



} //namespace Envoy
} //namespace Upstream
