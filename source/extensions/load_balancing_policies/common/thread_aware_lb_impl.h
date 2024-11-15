#pragma once

#include <bitset>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

using NormalizedHostWeightVector = std::vector<std::pair<HostConstSharedPtr, double>>;
using NormalizedHostWeightMap = std::map<HostConstSharedPtr, double>;

class ThreadAwareLoadBalancerBase : public LoadBalancerBase, public ThreadAwareLoadBalancer {
public:
  /**
   * Base class for a hashing load balancer implemented for use in a thread aware load balancer.
   * TODO(mattklein123): Currently only RingHash and Maglev use the thread aware load balancer.
   *                     The hash is pre-computed prior to getting to the real load balancer for
   *                     use in priority selection. In the future we likely we will want to pass
   *                     through the full load balancer context in case a future implementation
   *                     wants to use it.
   */
  class HashingLoadBalancer {
  public:
    virtual ~HashingLoadBalancer() = default;
    virtual HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const PURE;
    const absl::string_view hashKey(HostConstSharedPtr host, bool use_hostname) const {
      const ProtobufWkt::Value& val = Config::Metadata::metadataValue(
          host->metadata().get(), Config::MetadataFilters::get().ENVOY_LB,
          Config::MetadataEnvoyLbKeys::get().HASH_KEY);
      if (val.kind_case() != val.kStringValue && val.kind_case() != val.KIND_NOT_SET) {
        FINE_GRAIN_LOG(debug, "hash_key must be string type, got: {}",
                       static_cast<int>(val.kind_case()));
      }
      absl::string_view hash_key = val.string_value();
      if (hash_key.empty()) {
        hash_key = use_hostname ? host->hostname() : host->address()->asString();
      }
      return hash_key;
    }
  };
  using HashingLoadBalancerSharedPtr = std::shared_ptr<HashingLoadBalancer>;

  /**
   * Class for consistent hashing load balancer (CH-LB) with bounded loads.
   * It is common to both RingHash and Maglev load balancers, because the logic of selecting the
   * next host when one is overloaded is independent of the CH-LB type.
   */
  class BoundedLoadHashingLoadBalancer : public HashingLoadBalancer {
  public:
    BoundedLoadHashingLoadBalancer(HashingLoadBalancerSharedPtr hashing_lb_ptr,
                                   NormalizedHostWeightVector normalized_host_weights,
                                   uint32_t hash_balance_factor)
        : normalized_host_weights_map_(initNormalizedHostWeightMap(normalized_host_weights)),
          hashing_lb_ptr_(std::move(hashing_lb_ptr)),
          normalized_host_weights_(std::move(normalized_host_weights)),
          hash_balance_factor_(hash_balance_factor) {
      ASSERT(hashing_lb_ptr_ != nullptr);
      ASSERT(hash_balance_factor > 0);
    }
    HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

  protected:
    virtual double hostOverloadFactor(const Host& host, double weight) const;
    const NormalizedHostWeightMap normalized_host_weights_map_;

  private:
    const NormalizedHostWeightMap
    initNormalizedHostWeightMap(const NormalizedHostWeightVector& normalized_host_weights) {
      NormalizedHostWeightMap normalized_host_weights_map;
      for (auto const& item : normalized_host_weights) {
        normalized_host_weights_map[item.first] = item.second;
      }
      return normalized_host_weights_map;
    }
    const HashingLoadBalancerSharedPtr hashing_lb_ptr_;
    const NormalizedHostWeightVector normalized_host_weights_;
    const uint32_t hash_balance_factor_;
  };
  // Upstream::ThreadAwareLoadBalancer
  LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override;

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext*) override { return nullptr; }
  // Preconnect not implemented for hash based load balancing
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }
  // Pool selection not implemented.
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                           const Upstream::Host& /*host*/,
                           std::vector<uint8_t>& /*hash_key*/) override {
    return absl::nullopt;
  }
  // Lifetime tracking not implemented.
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

protected:
  ThreadAwareLoadBalancerBase(const PrioritySet& priority_set, ClusterLbStats& stats,
                              Runtime::Loader& runtime, Random::RandomGenerator& random,
                              uint32_t healthy_panic_threshold, bool locality_weighted_balancing)
      : LoadBalancerBase(priority_set, stats, runtime, random, healthy_panic_threshold),
        factory_(new LoadBalancerFactoryImpl(stats, random)),
        locality_weighted_balancing_(locality_weighted_balancing) {}

private:
  struct PerPriorityState {
    std::shared_ptr<HashingLoadBalancer> current_lb_;
    bool global_panic_{};
  };
  using PerPriorityStatePtr = std::unique_ptr<PerPriorityState>;

  struct LoadBalancerImpl : public LoadBalancer {
    LoadBalancerImpl(ClusterLbStats& stats, Random::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
    // Preconnect not implemented for hash based load balancing
    HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return absl::nullopt;
    }
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    ClusterLbStats& stats_;
    Random::RandomGenerator& random_;
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_;
    std::shared_ptr<HealthyLoad> healthy_per_priority_load_;
    std::shared_ptr<DegradedLoad> degraded_per_priority_load_;
  };

  struct LoadBalancerFactoryImpl : public LoadBalancerFactory {
    LoadBalancerFactoryImpl(ClusterLbStats& stats, Random::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancerFactory
    // Ignore the params for the thread-aware LB.
    LoadBalancerPtr create(LoadBalancerParams) override;

    ClusterLbStats& stats_;
    Random::RandomGenerator& random_;
    absl::Mutex mutex_;
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_ ABSL_GUARDED_BY(mutex_);
    // This is split out of PerPriorityState so LoadBalancerBase::ChoosePriority can be reused.
    std::shared_ptr<HealthyLoad> healthy_per_priority_load_ ABSL_GUARDED_BY(mutex_);
    std::shared_ptr<DegradedLoad> degraded_per_priority_load_ ABSL_GUARDED_BY(mutex_);
  };

  virtual HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double min_normalized_weight, double max_normalized_weight) PURE;
  absl::Status refresh();

  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
  const bool locality_weighted_balancing_{};
  Common::CallbackHandlePtr priority_update_cb_;
};

} // namespace Upstream
} // namespace Envoy
