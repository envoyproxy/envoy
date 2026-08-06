#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/extensions/load_balancing_policies/header_order/v3/header_order.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HeaderOrder {

using ::envoy::extensions::load_balancing_policies::header_order::v3::HeaderOrder;

using ::Envoy::Random::RandomGenerator;
using ::Envoy::Runtime::Loader;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::ClusterInfo;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSelectionResponse;
using ::Envoy::Upstream::LoadBalancerConfigPtr;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerFactorySharedPtr;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::PrioritySet;
using ::Envoy::Upstream::ThreadAwareLoadBalancerPtr;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

// Parsed configuration. Holds the metadata namespace/key plus the factory and parsed config for
// the required `fallback_policy`, which is used both (a) as the child LB that actually selects
// a host *within* whichever priority this policy chooses for a given attempt, and (b) as a full
// fallback whenever the metadata is absent/malformed or no listed priority is usable.
class HeaderOrderLbConfig : public Upstream::LoadBalancerConfig {
public:
  static absl::StatusOr<std::unique_ptr<HeaderOrderLbConfig>> make(const HeaderOrder& config,
                                                                    ServerFactoryContext& context);

  ThreadAwareLoadBalancerPtr create(const ClusterInfo& cluster_info,
                                    const PrioritySet& priority_set, Loader& runtime,
                                    RandomGenerator& random, TimeSource& time_source) const;

  const std::string& metadataNamespace() const { return metadata_namespace_; }
  const std::string& metadataKey() const { return metadata_key_; }

private:
  HeaderOrderLbConfig(std::string&& metadata_namespace, std::string&& metadata_key,
                      TypedLoadBalancerFactory* fallback_load_balancer_factory,
                      LoadBalancerConfigPtr&& fallback_load_balancer_config);

  // Group the factory and config together to make them const in the configuration object.
  struct FallbackLbConfig {
    TypedLoadBalancerFactory* const load_balancer_factory = nullptr;
    const LoadBalancerConfigPtr load_balancer_config;
  };
  const FallbackLbConfig fallback_lb_config_;

  const std::string metadata_namespace_;
  const std::string metadata_key_;
};

// Load balancer that reads a per-request, ordered list of Envoy priority indices from dynamic
// metadata and forces every host-selection attempt -- the initial attempt and every retry
// alike -- to target the priority at the corresponding position in that list, indexed by the
// request's native upstream attempt count (StreamInfo::attemptCount()). See header_order.proto
// for full semantics. Delegates all actual host selection (within the chosen priority, and as a
// full fallback) to the configured `fallback_policy` LB.
class HeaderOrderLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                protected Logger::Loggable<Logger::Id::upstream> {
public:
  HeaderOrderLoadBalancer(const HeaderOrderLbConfig& config,
                          ThreadAwareLoadBalancerPtr fallback_lb)
      : config_(config), fallback_lb_(std::move(fallback_lb)) {}

  LoadBalancerFactorySharedPtr factory() override;

  absl::Status initialize() override;

private:
  // Thread-local LB implementation.
  class LoadBalancerImpl : public Upstream::LoadBalancer {
  public:
    LoadBalancerImpl(const HeaderOrderLbConfig& config,
                     LoadBalancerFactorySharedPtr fallback_lb_factory, LoadBalancerParams params);

    HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override {
      return fallback_lb_->peekAnotherHost(context);
    }

    HostSelectionResponse chooseHost(LoadBalancerContext* context) override;

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return fallback_lb_->lifetimeCallbacks();
    }

    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(LoadBalancerContext* context, const Upstream::Host& host,
                             std::vector<uint8_t>& hash_key) override {
      return fallback_lb_->selectExistingConnection(context, host, hash_key);
    }

  private:
    // Parses the configured metadata (if any, on the given context's request stream info) into
    // an ordered list of priority indices. Returns an empty vector if the metadata is absent,
    // empty, or malformed.
    std::vector<uint32_t> parseOrder(LoadBalancerContext* context) const;

    const HeaderOrderLbConfig& config_;
    // Held so a new worker-local fallback LB can be created if the inner factory still asks for
    // the legacy recreate-on-host-change semantics (see member_update_cb_ below).
    const LoadBalancerFactorySharedPtr fallback_lb_factory_;
    LoadBalancerPtr fallback_lb_;
    const PrioritySet& priority_set_;
    const PrioritySet* const local_priority_set_{};
    Common::CallbackHandlePtr member_update_cb_;
  };

  // LoadBalancerFactory implementation shared by worker threads to create thread-local LB
  // instances. Shared state in this class MUST be protected by appropriate mutexes.
  class LoadBalancerFactoryImpl : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactoryImpl(const HeaderOrderLbConfig& config,
                            LoadBalancerFactorySharedPtr fallback_lb_factory)
        : config_(config), fallback_lb_factory_(std::move(fallback_lb_factory)) {}

    LoadBalancerPtr create(LoadBalancerParams params) override;

    bool recreateOnHostChangeDeprecated() const override { return false; }

  private:
    const HeaderOrderLbConfig& config_;
    const LoadBalancerFactorySharedPtr fallback_lb_factory_;
  };

  const HeaderOrderLbConfig& config_;
  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
  const ThreadAwareLoadBalancerPtr fallback_lb_;
};

} // namespace HeaderOrder
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
