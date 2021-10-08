#include <memory>

#include "test/common/upstream/least_request_load_balancer_fuzz.pb.validate.h"
#include "test/common/upstream/zone_aware_load_balancer_fuzz_base.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

// Least Request takes into account both weights (handled in ZoneAwareLoadBalancerFuzzBase), and
// requests active as well
void setRequestsActiveForStaticHosts(NiceMock<MockPrioritySet>& priority_set,
                                     const std::string& random_bytestring) {
  uint32_t index_of_random_bytestring = 0;
  // Iterate through all the current host sets and set requests for each
  for (uint32_t priority_level = 0; priority_level < priority_set.hostSetsPerPriority().size();
       ++priority_level) {
    MockHostSet& host_set = *priority_set.getMockHostSet(priority_level);
    for (auto& host : host_set.hosts_) {
      // Make sure no weights persisted from previous fuzz iterations
      ASSERT(host->stats().rq_active_.value() == 0);
      host->stats().rq_active_.set(
          random_bytestring[index_of_random_bytestring % random_bytestring.length()] % 3);
      ++index_of_random_bytestring;
    }
  }
}

void removeRequestsActiveForStaticHosts(NiceMock<MockPrioritySet>& priority_set) {
  // Clear out any set requests active
  for (uint32_t priority_level = 0; priority_level < priority_set.hostSetsPerPriority().size();
       ++priority_level) {
    MockHostSet& host_set = *priority_set.getMockHostSet(priority_level);
    for (auto& host : host_set.hosts_) {
      host->stats().rq_active_.set(0);
    }
  }
}

DEFINE_PROTO_FUZZER(const test::common::upstream::LeastRequestLoadBalancerTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  const test::common::upstream::ZoneAwareLoadBalancerTestCase& zone_aware_load_balancer_test_case =
      input.zone_aware_load_balancer_test_case();

  ZoneAwareLoadBalancerFuzzBase zone_aware_load_balancer_fuzz = ZoneAwareLoadBalancerFuzzBase(
      zone_aware_load_balancer_test_case.need_local_priority_set(),
      zone_aware_load_balancer_test_case.random_bytestring_for_weights());
  zone_aware_load_balancer_fuzz.initializeLbComponents(
      zone_aware_load_balancer_test_case.load_balancer_test_case());

  setRequestsActiveForStaticHosts(zone_aware_load_balancer_fuzz.priority_set_,
                                  input.random_bytestring_for_requests());

  try {
    zone_aware_load_balancer_fuzz.lb_ = std::make_unique<LeastRequestLoadBalancer>(
        zone_aware_load_balancer_fuzz.priority_set_,
        zone_aware_load_balancer_fuzz.local_priority_set_.get(),
        zone_aware_load_balancer_fuzz.stats_, zone_aware_load_balancer_fuzz.runtime_,
        zone_aware_load_balancer_fuzz.random_,
        zone_aware_load_balancer_test_case.load_balancer_test_case().common_lb_config(),
        input.least_request_lb_config(), zone_aware_load_balancer_fuzz.simTime());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException; {}", e.what());
    removeRequestsActiveForStaticHosts(zone_aware_load_balancer_fuzz.priority_set_);
    return;
  }

  zone_aware_load_balancer_fuzz.replay(
      zone_aware_load_balancer_test_case.load_balancer_test_case().actions());

  removeRequestsActiveForStaticHosts(zone_aware_load_balancer_fuzz.priority_set_);
}

} // namespace Upstream
} // namespace Envoy
