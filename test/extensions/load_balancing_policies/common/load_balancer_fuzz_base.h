#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/upstream/load_balancer_context_base.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/random.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Upstream {

// This class implements replay logic, and also handles the initial setup of static host sets and
// the subsequent updates to those sets.
class LoadBalancerFuzzBase {
public:
  LoadBalancerFuzzBase()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, *stats_store_.rootScope()){};

  // Initializes load balancer components shared amongst every load balancer, random_, and
  // priority_set_
  virtual void initializeLbComponents(const test::common::upstream::LoadBalancerTestCase& input);
  virtual void
  updateHealthFlagsForAHostSet(const uint64_t host_priority, const uint32_t num_healthy_hosts,
                               const uint32_t num_degraded_hosts, const uint32_t num_excluded_hosts,
                               const Protobuf::RepeatedField<Protobuf::uint32>& random_bytestring);
  // These two actions have a lot of logic attached to them. However, all the logic that the load
  // balancer needs to run its algorithm is already encapsulated within the load balancer. Thus,
  // once the load balancer is constructed, all this class has to do is call lb_->peekAnotherHost()
  // and lb_->chooseHost().
  void preconnect();
  void chooseHost();
  void replay(const Protobuf::RepeatedPtrField<test::common::upstream::LbAction>& actions);

  // These public objects shared amongst all types of load balancers will be used to construct load
  // balancers in specific load balancer fuzz classes
  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  Random::PsuedoRandomGenerator64 random_;
  NiceMock<MockPrioritySet> priority_set_;
  std::unique_ptr<LoadBalancer> lb_;

  virtual ~LoadBalancerFuzzBase() {
    // In an iteration, after this class is destructed, whether through an exception throw or
    // finishing an action stream, must clear any state that could persist in static hosts.
    // The only outstanding health flags set are those that are set from hosts being placed in
    // degraded and excluded. Thus, use the priority set pointer to know which flags to clear.
    for (uint32_t priority_level = 0; priority_level < priority_set_.hostSetsPerPriority().size();
         ++priority_level) {
      MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
      for (auto& host : host_set.degraded_hosts_) {
        host->healthFlagClear(Host::HealthFlag::DEGRADED_ACTIVE_HC);
      }
      for (auto& host : host_set.excluded_hosts_) {
        host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      }
    }
  };

protected:
  // Untrusted upstreams don't have the ability to change the host set size, so keep it constant
  // over the fuzz iteration.
  virtual void
  initializeASingleHostSet(const test::common::upstream::SetupPriorityLevel& setup_priority_level,
                           const uint8_t priority_level, uint16_t& port);

  // This is used to choose a host set to update the flags in an update flags event by modding a
  // random uint64 against this number.
  uint8_t num_priority_levels_ = 0;

private:
  // This map used when updating health flags - making sure the health flags are updated hosts in
  // localities Key - index of host within full host list, value - locality level host at index is
  // in
  absl::node_hash_map<uint8_t, uint8_t> locality_indexes_;

  static HostVector initializeHostsForUseInFuzzing(std::shared_ptr<MockClusterInfo> info);

  // Will statically initialize 60000 hosts in this vector. Will have to clear these static
  // hosts flags at the end of each fuzz iteration
  HostVector initialized_hosts_;
};

} // namespace Upstream
} // namespace Envoy
