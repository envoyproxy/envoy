#include "test/extensions/load_balancing_policies/override_host/test_lb.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/protobuf/protobuf.h"

#include "test/extensions/load_balancing_policies/override_host/test_lb.pb.h"

#include "absl/log/check.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicForwarding {

using ::Envoy::Upstream::Host;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSelectionResponse;
using ::Envoy::Upstream::HostSetPtr;
using ::Envoy::Upstream::HostVector;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerConfigPtr;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerFactorySharedPtr;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::PrioritySet;

class TestLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  TestLoadBalancer() : factory_(std::make_shared<LoadBalancerFactoryImpl>()) {}

  LoadBalancerFactorySharedPtr factory() override { return factory_; };

  absl::Status initialize() override { return absl::OkStatus(); }

private:
  class LoadBalancerImpl : public Upstream::LoadBalancer {
  public:
    explicit LoadBalancerImpl(const PrioritySet& priority_set) : priority_set_(priority_set) {}

    HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }

    HostSelectionResponse chooseHost(LoadBalancerContext*) override {
      const std::vector<HostSetPtr>& priority_sets = priority_set_.hostSetsPerPriority();
      if (priority_sets.empty()) {
        return {nullptr};
      }
      HostVector hosts = priority_sets[0]->hosts();
      if (hosts.empty()) {
        return {nullptr};
      }
      return {hosts[0]};
    }

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(LoadBalancerContext*, const Host&, std::vector<uint8_t>&) override {
      return std::nullopt;
    }

  private:
    const PrioritySet& priority_set_;
  };

  // LoadBalancerFactory implementation shared by worker threads to create
  // thread-local LB instances. Shared state in this class MUST be protected by
  // appropriate mutexes.
  class LoadBalancerFactoryImpl : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactoryImpl() = default;

    // Called by worker threads to create a thread-local load balancer.
    LoadBalancerPtr create(LoadBalancerParams params) override {
      return std::make_unique<LoadBalancerImpl>(params.priority_set);
    }
  };

  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
};

absl::StatusOr<LoadBalancerConfigPtr>
TestLoadBalancerFactory::loadConfig(Server::Configuration::ServerFactoryContext&,
                                    const Protobuf::Message&) {
  return std::make_unique<LoadBalancerConfig>();
}

Upstream::ThreadAwareLoadBalancerPtr
TestLoadBalancerFactory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                const ClusterInfo&, const PrioritySet&, Loader&, RandomGenerator&,
                                TimeSource&) {
  ASSERT(lb_config.has_value());
  return std::make_unique<TestLoadBalancer>();
}

REGISTER_FACTORY(TestLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace DynamicForwarding
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
