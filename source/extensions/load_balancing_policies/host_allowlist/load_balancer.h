#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/extensions/load_balancing_policies/host_allowlist/v3/host_allowlist.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HostAllowlist {

using HostAllowlistProto =
    envoy::extensions::load_balancing_policies::host_allowlist::v3::HostAllowlist;
using ::Envoy::Random::RandomGenerator;
using ::Envoy::Runtime::Loader;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::ClusterInfo;
using ::Envoy::Upstream::Host;
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

inline constexpr absl::string_view kDefaultHostMetadataNamespace = "envoy.lb";

#define ALL_HOST_ALLOWLIST_STATS(COUNTER)                                                          \
  COUNTER(rq_lb_attempt_exhausted)                                                                 \
  COUNTER(rq_metadata_missing)

struct HostAllowlistStats {
  ALL_HOST_ALLOWLIST_STATS(GENERATE_COUNTER_STRUCT)
};

using HostAllowlistStatsSharedPtr = std::shared_ptr<HostAllowlistStats>;

class HostAllowlistLbConfig : public Upstream::LoadBalancerConfig {
public:
  static absl::StatusOr<std::unique_ptr<HostAllowlistLbConfig>>
  make(const HostAllowlistProto& config, ServerFactoryContext& context);

  ThreadAwareLoadBalancerPtr create(const ClusterInfo& cluster_info,
                                    const PrioritySet& priority_set, Loader& runtime,
                                    RandomGenerator& random, TimeSource& time_source) const;

  const std::string& metadataNamespace() const { return metadata_namespace_; }
  const std::string& metadataKey() const { return metadata_key_; }
  const std::string& hostMetadataNamespace() const { return host_metadata_namespace_; }
  const std::vector<std::string>& hostMetadataKeys() const { return host_metadata_keys_; }
  bool failClosedOnMissingMetadata() const { return fail_closed_on_missing_metadata_; }

private:
  HostAllowlistLbConfig(std::string metadata_namespace, std::string metadata_key,
                        std::string host_metadata_namespace,
                        std::vector<std::string> host_metadata_keys,
                        bool fail_closed_on_missing_metadata,
                        TypedLoadBalancerFactory* child_load_balancer_factory,
                        LoadBalancerConfigPtr&& child_load_balancer_config);

  struct ChildLbConfig {
    TypedLoadBalancerFactory* const load_balancer_factory = nullptr;
    const LoadBalancerConfigPtr load_balancer_config;
  };
  const ChildLbConfig child_picker_lb_config_;
  const std::string metadata_namespace_;
  const std::string metadata_key_;
  const std::string host_metadata_namespace_;
  const std::vector<std::string> host_metadata_keys_;
  const bool fail_closed_on_missing_metadata_;
};

class HostAllowlistLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                  protected Logger::Loggable<Logger::Id::upstream> {
public:
  HostAllowlistLoadBalancer(const HostAllowlistLbConfig& config,
                            ThreadAwareLoadBalancerPtr child_picker_lb, Stats::Scope& scope);

  LoadBalancerFactorySharedPtr factory() override;
  absl::Status initialize() override;

private:
  class LoadBalancerImpl : public Upstream::LoadBalancer {
  public:
    LoadBalancerImpl(const HostAllowlistLbConfig& config,
                     LoadBalancerFactorySharedPtr child_picker_lb_factory,
                     HostAllowlistStatsSharedPtr stats, LoadBalancerParams params);

    HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;
    HostSelectionResponse chooseHost(LoadBalancerContext* context) override;

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return child_picker_lb_->lifetimeCallbacks();
    }

    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(LoadBalancerContext* context, const Host& host,
                             std::vector<uint8_t>& hash_key) override {
      return child_picker_lb_->selectExistingConnection(context, host, hash_key);
    }

  private:
    const HostAllowlistLbConfig& config_;
    const LoadBalancerFactorySharedPtr child_picker_lb_factory_;
    const HostAllowlistStatsSharedPtr stats_;
    LoadBalancerPtr child_picker_lb_;
    const PrioritySet& priority_set_;
    const PrioritySet* const local_priority_set_{};
    Common::CallbackHandlePtr member_update_cb_;
  };

  class LoadBalancerFactoryImpl : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactoryImpl(const HostAllowlistLbConfig& config,
                            LoadBalancerFactorySharedPtr child_picker_lb_factory,
                            HostAllowlistStatsSharedPtr stats)
        : config_(config), child_picker_lb_factory_(std::move(child_picker_lb_factory)),
          stats_(std::move(stats)) {}

    LoadBalancerPtr create(LoadBalancerParams params) override;
    bool recreateOnHostChangeDeprecated() const override { return false; }

  private:
    const HostAllowlistLbConfig& config_;
    LoadBalancerFactorySharedPtr child_picker_lb_factory_;
    HostAllowlistStatsSharedPtr stats_;
  };

  const HostAllowlistLbConfig& config_;
  const HostAllowlistStatsSharedPtr stats_;
  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
  const ThreadAwareLoadBalancerPtr child_picker_lb_;
};

} // namespace HostAllowlist
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
