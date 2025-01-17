#include <memory>

#include "envoy/common/optref.h"

#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "test/extensions/load_balancing_policies/common/zone_aware_load_balancer_fuzz_base.h"
#include "test/extensions/load_balancing_policies/round_robin/round_robin_load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::RoundRobinLoadBalancerTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  const test::common::upstream::ZoneAwareLoadBalancerTestCase& zone_aware_load_balancer_test_case =
      input.zone_aware_load_balancer_test_case();

  // Validate the correctness of the Slow-Start config values.
  if (input.has_round_robin_lb_config() && input.round_robin_lb_config().has_slow_start_config()) {
    uint32_t num_hosts = 0;
    for (const auto& setup_priority_level :
         zone_aware_load_balancer_test_case.load_balancer_test_case().setup_priority_levels()) {
      num_hosts += setup_priority_level.num_hosts_in_priority_level();
    }
    if (!ZoneAwareLoadBalancerFuzzBase::validateSlowStart(
            input.round_robin_lb_config().slow_start_config(), num_hosts)) {
      return;
    }
  }

  ZoneAwareLoadBalancerFuzzBase zone_aware_load_balancer_fuzz = ZoneAwareLoadBalancerFuzzBase(
      zone_aware_load_balancer_test_case.need_local_priority_set(),
      zone_aware_load_balancer_test_case.random_bytestring_for_weights());
  zone_aware_load_balancer_fuzz.initializeLbComponents(
      zone_aware_load_balancer_test_case.load_balancer_test_case());

  try {
    zone_aware_load_balancer_fuzz.lb_ = std::make_unique<RoundRobinLoadBalancer>(
        zone_aware_load_balancer_fuzz.priority_set_,
        zone_aware_load_balancer_fuzz.local_priority_set_.get(),
        zone_aware_load_balancer_fuzz.stats_, zone_aware_load_balancer_fuzz.runtime_,
        zone_aware_load_balancer_fuzz.random_,
        zone_aware_load_balancer_test_case.load_balancer_test_case().common_lb_config(),
        makeOptRef<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig>(
            input.round_robin_lb_config()),
        zone_aware_load_balancer_fuzz.simTime());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException; {}", e.what());
    return;
  }

  zone_aware_load_balancer_fuzz.replay(
      zone_aware_load_balancer_test_case.load_balancer_test_case().actions());
}

} // namespace Upstream
} // namespace Envoy
