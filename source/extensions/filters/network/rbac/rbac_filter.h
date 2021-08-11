#pragma once

#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"
#include "source/extensions/filters/common/rbac/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

enum EngineResult { Unknown, None, Allow, Deny };

struct Result {
  EngineResult engine_result_;
  std::string connection_termination_details_;
};

/**
 * Configuration for the RBAC network filter.
 */
class RoleBasedAccessControlFilterConfig {
public:
  RoleBasedAccessControlFilterConfig(
      const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config, Stats::Scope& scope);

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats& stats() { return stats_; }
  std::string shadowEffectivePolicyIdField() const {
    return shadow_rules_stat_prefix_ +
           Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEffectivePolicyIdField;
  }
  std::string shadowEngineResultField() const {
    return shadow_rules_stat_prefix_ +
           Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEngineResultField;
  }

  const Filters::Common::RBAC::RoleBasedAccessControlEngineImpl*
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_.get()
                                                                    : shadow_engine_.get();
  }

  envoy::extensions::filters::network::rbac::v3::RBAC::EnforcementType enforcementType() const {
    return enforcement_type_;
  }

private:
  Filters::Common::RBAC::RoleBasedAccessControlFilterStats stats_;
  const std::string shadow_rules_stat_prefix_;

  std::unique_ptr<const Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> engine_;
  std::unique_ptr<const Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> shadow_engine_;
  const envoy::extensions::filters::network::rbac::v3::RBAC::EnforcementType enforcement_type_;
};

using RoleBasedAccessControlFilterConfigSharedPtr =
    std::shared_ptr<RoleBasedAccessControlFilterConfig>;

/**
 * Implementation of a basic RBAC network filter.
 */
class RoleBasedAccessControlFilter : public Network::ReadFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {

public:
  RoleBasedAccessControlFilter(RoleBasedAccessControlFilterConfigSharedPtr config)
      : config_(config) {}
  ~RoleBasedAccessControlFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; };
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  void setDynamicMetadata(std::string shadow_engine_result, std::string shadow_policy_id);

private:
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* callbacks_{};
  EngineResult engine_result_{Unknown};
  EngineResult shadow_engine_result_{Unknown};

  Result checkEngine(Filters::Common::RBAC::EnforcementMode mode);
};

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
