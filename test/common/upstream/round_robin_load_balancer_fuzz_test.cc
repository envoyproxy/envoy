#include <memory>

#include "test/common/upstream/load_balancer_fuzz_base.h"
#include "test/common/upstream/round_robin_load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::RoundRobinLoadBalancerTestCase input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  std::unique_ptr<LoadBalancerFuzzBase> load_balancer_fuzz =
      std::make_unique<LoadBalancerFuzzBase>();
  load_balancer_fuzz->initializeLbComponents(input.load_balancer_test_case());

  try {
    if (!input.need_local_cluster()) { // construct the load balancer without a local priority set
      load_balancer_fuzz->lb_ = std::make_unique<RoundRobinLoadBalancer>(
          load_balancer_fuzz->priority_set_, nullptr, load_balancer_fuzz->stats_,
          load_balancer_fuzz->runtime_, load_balancer_fuzz->random_,
          input.load_balancer_test_case().common_lb_config());
    } else { // construct the load balancer with a local priority set
      std::shared_ptr<PrioritySetImpl> local_priority_set = std::make_shared<PrioritySetImpl>();
      local_priority_set->getOrCreateHostSet(0);
      load_balancer_fuzz->lb_ = std::make_unique<RoundRobinLoadBalancer>(
          load_balancer_fuzz->priority_set_, local_priority_set.get(), load_balancer_fuzz->stats_,
          load_balancer_fuzz->runtime_, load_balancer_fuzz->random_,
          input.load_balancer_test_case().common_lb_config());
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException; {}", e.what());
    return;
  }

  load_balancer_fuzz->replay(input.load_balancer_test_case().actions());
}

} // namespace Upstream
} // namespace Envoy
