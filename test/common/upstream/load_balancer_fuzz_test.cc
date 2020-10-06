// Mod it across host sets to determine which one to use

#include "test/common/upstream/load_balancer_fuzz.h"
#include "test/common/upstream/load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::LoadBalancerTestCase input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }
  std::unique_ptr<LoadBalancerFuzzBase> load_balancer_fuzz;

  // TODO: Switch across type once added more
  load_balancer_fuzz = std::make_unique<RandomLoadBalancerFuzzTest>();

  load_balancer_fuzz->initializeAndReplay(input);
}

} // namespace Upstream
} // namespace Envoy
