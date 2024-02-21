#include <memory>

#include "envoy/common/optref.h"

#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/common/upstream/round_robin_load_balancer_fuzz.pb.validate.h"
#include "test/common/upstream/zone_aware_load_balancer_fuzz_base.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/server/factory_context.h"
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

  // Load the config using the factory (does extra validation).
  try {
    envoy::config::cluster::v3::Cluster cluster;
    cluster.mutable_common_lb_config()->CopyFrom(
        zone_aware_load_balancer_test_case.load_balancer_test_case().common_lb_config());
    cluster.mutable_round_robin_lb_config()->CopyFrom(input.round_robin_lb_config());
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    Envoy::Extensions::LoadBalancingPolices::RoundRobin::Factory factory;
    auto lb_config = factory.loadConfig(cluster, context.messageValidationVisitor());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException while loading config; {}", e.what());
    return;
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
