#pragma once

#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/upstream/load_balancer_factory_base.h"

#include "test/extensions/load_balancing_policies/override_host/test_lb.pb.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicForwarding {

constexpr absl::string_view kTestLoadBalancerName = "envoy.load_balancers.override_host.test";

using ::Envoy::Random::RandomGenerator;
using ::Envoy::Runtime::Loader;
using ::Envoy::Upstream::ClusterInfo;
using ::Envoy::Upstream::PrioritySet;
using ::test::load_balancing_policies::override_host::Config;

// This is a test load balancing policy extension that is used to validate that
// the dynamic forwarding load balancing is correctly calling LB configured in
// `locality_picking_policy` for the initial locality picking.
class TestLoadBalancerFactory : public Upstream::TypedLoadBalancerFactoryBase<Config> {
public:
  TestLoadBalancerFactory() : TypedLoadBalancerFactoryBase(std::string(kTestLoadBalancerName)) {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override;

private:
  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const ClusterInfo& cluster_info,
                                              const PrioritySet& priority_set, Loader& runtime,
                                              RandomGenerator& random,
                                              TimeSource& time_source) override;
};

} // namespace DynamicForwarding
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
