#pragma once

#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/extensions/load_balancing_policies/quota_aware/v3/quota_aware.pb.h"
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
namespace QuotaAware {

using QuotaAwareProto = envoy::extensions::load_balancing_policies::quota_aware::v3::QuotaAware;
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

inline constexpr absl::string_view kDefaultMetadataNamespace = "envoy.filters.http.ratelimit";
inline constexpr absl::string_view kDefaultCandidatesKey = "candidates";
inline constexpr absl::string_view kDefaultHostMetadataNamespace = "envoy.lb";
inline constexpr absl::string_view kDefaultHostIdKey = "id";
inline constexpr absl::string_view kDefaultHostSecondaryIdKey = "secondary_id";
inline constexpr absl::string_view kDefaultCandidateIdField = "id";
inline constexpr absl::string_view kDefaultCandidateSecondaryIdField = "secondary_id";

#define ALL_QUOTA_AWARE_STATS(COUNTER)                                                             \
  COUNTER(rq_exhausted)                                                                            \
  COUNTER(rq_metadata_missing)

struct QuotaAwareStats {
  ALL_QUOTA_AWARE_STATS(GENERATE_COUNTER_STRUCT)
};

using QuotaAwareStatsSharedPtr = std::shared_ptr<QuotaAwareStats>;

class QuotaAwareLbConfig : public Upstream::LoadBalancerConfig {
public:
  static absl::StatusOr<std::unique_ptr<QuotaAwareLbConfig>> make(const QuotaAwareProto& config,
                                                                  ServerFactoryContext& context);

  ThreadAwareLoadBalancerPtr create(const ClusterInfo& cluster_info,
                                    const PrioritySet& priority_set, Loader& runtime,
                                    RandomGenerator& random, TimeSource& time_source) const;

  const std::string& metadataNamespace() const { return metadata_namespace_; }
  const std::string& candidatesKey() const { return candidates_key_; }
  const std::string& hostMetadataNamespace() const { return host_metadata_namespace_; }
  const std::string& hostIdKey() const { return host_id_key_; }
  const std::string& hostSecondaryIdKey() const { return host_secondary_id_key_; }
  const std::string& candidateIdField() const { return candidate_id_field_; }
  const std::string& candidateSecondaryIdField() const { return candidate_secondary_id_field_; }
  bool failClosedOnMissingMetadata() const { return fail_closed_on_missing_metadata_; }

private:
  QuotaAwareLbConfig(std::string metadata_namespace, std::string candidates_key,
                     std::string host_metadata_namespace, std::string host_id_key,
                     std::string host_secondary_id_key, std::string candidate_id_field,
                     std::string candidate_secondary_id_field,
                     bool fail_closed_on_missing_metadata,
                     TypedLoadBalancerFactory* fallback_load_balancer_factory,
                     LoadBalancerConfigPtr&& fallback_load_balancer_config);

  struct FallbackLbConfig {
    TypedLoadBalancerFactory* const load_balancer_factory = nullptr;
    const LoadBalancerConfigPtr load_balancer_config;
  };
  const FallbackLbConfig fallback_picker_lb_config_;
  const std::string metadata_namespace_;
  const std::string candidates_key_;
  const std::string host_metadata_namespace_;
  const std::string host_id_key_;
  const std::string host_secondary_id_key_;
  const std::string candidate_id_field_;
  const std::string candidate_secondary_id_field_;
  const bool fail_closed_on_missing_metadata_;
};

class QuotaAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                               protected Logger::Loggable<Logger::Id::upstream> {
public:
  QuotaAwareLoadBalancer(const QuotaAwareLbConfig& config,
                         ThreadAwareLoadBalancerPtr fallback_picker_lb, Stats::Scope& scope);

  LoadBalancerFactorySharedPtr factory() override;
  absl::Status initialize() override;

private:
  class LoadBalancerImpl : public Upstream::LoadBalancer {
  public:
    LoadBalancerImpl(const QuotaAwareLbConfig& config,
                     LoadBalancerFactorySharedPtr fallback_picker_lb_factory,
                     QuotaAwareStatsSharedPtr stats, LoadBalancerParams params);

    HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;
    HostSelectionResponse chooseHost(LoadBalancerContext* context) override;

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return fallback_picker_lb_->lifetimeCallbacks();
    }

    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(LoadBalancerContext* context, const Host& host,
                             std::vector<uint8_t>& hash_key) override {
      return fallback_picker_lb_->selectExistingConnection(context, host, hash_key);
    }

  private:
    const QuotaAwareLbConfig& config_;
    const LoadBalancerFactorySharedPtr fallback_picker_lb_factory_;
    const QuotaAwareStatsSharedPtr stats_;
    LoadBalancerPtr fallback_picker_lb_;
    const PrioritySet& priority_set_;
    const PrioritySet* const local_priority_set_{};
    Common::CallbackHandlePtr member_update_cb_;
  };

  class LoadBalancerFactoryImpl : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactoryImpl(const QuotaAwareLbConfig& config,
                            LoadBalancerFactorySharedPtr fallback_picker_lb_factory,
                            QuotaAwareStatsSharedPtr stats)
        : config_(config), fallback_picker_lb_factory_(std::move(fallback_picker_lb_factory)),
          stats_(std::move(stats)) {}

    LoadBalancerPtr create(LoadBalancerParams params) override;
    bool recreateOnHostChangeDeprecated() const override { return false; }

  private:
    const QuotaAwareLbConfig& config_;
    LoadBalancerFactorySharedPtr fallback_picker_lb_factory_;
    QuotaAwareStatsSharedPtr stats_;
  };

  const QuotaAwareLbConfig& config_;
  const QuotaAwareStatsSharedPtr stats_;
  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
  const ThreadAwareLoadBalancerPtr fallback_picker_lb_;
};

} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
