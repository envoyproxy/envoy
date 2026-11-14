#pragma once

#include <memory>
#include <optional>

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"
#include "source/extensions/filters/common/rbac/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

// Dynamic metadata namespace the filter writes its engine results under.
inline constexpr absl::string_view DynamicMetadataKey = "envoy.filters.http.rbac";

class ActionValidationVisitor final : public Filters::Common::RBAC::ActionValidationVisitor {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>& data_input,
                             absl::string_view type_url) override;

private:
  static const absl::flat_hash_set<std::string> allowed_inputs_set_;
};

// The two dynamic metadata field names one engine writes, built once from its stat prefix.
struct DynamicMetadataFieldNames {
  std::string effective_policy_id_;
  std::string engine_result_;
};

class RoleBasedAccessControlRouteSpecificFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  RoleBasedAccessControlRouteSpecificFilterConfig(
      const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& per_route_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  const Filters::Common::RBAC::RoleBasedAccessControlEngine*
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_.get()
                                                                    : shadow_engine_.get();
  }

  bool perRuleStatsEnabled() const { return per_rule_stats_; }

  // The field names of one engine, set only when this config's stat prefix for that engine is
  // non-empty and so overrides the filter config's.
  const std::optional<DynamicMetadataFieldNames>&
  fieldNames(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? enforced_field_names_
                                                                    : shadow_field_names_;
  }

private:
  const std::optional<DynamicMetadataFieldNames> enforced_field_names_;
  const std::optional<DynamicMetadataFieldNames> shadow_field_names_;
  ActionValidationVisitor action_validation_visitor_;
  std::unique_ptr<Filters::Common::RBAC::RoleBasedAccessControlEngine> engine_;
  std::unique_ptr<Filters::Common::RBAC::RoleBasedAccessControlEngine> shadow_engine_;
  const bool per_rule_stats_;
};

/**
 * Configuration for the RBAC filter.
 */
class RoleBasedAccessControlFilterConfig {
public:
  RoleBasedAccessControlFilterConfig(
      const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats& stats() { return stats_; }

  // Each getter below takes the route's most specific config, or nullptr when the route has none.
  // The field names come from the route config when its stat prefix for the engine is non-empty,
  // and from this config otherwise.
  const DynamicMetadataFieldNames&
  fieldNames(const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
             Filters::Common::RBAC::EnforcementMode mode) const;

  const Filters::Common::RBAC::RoleBasedAccessControlEngine*
  engine(const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
         Filters::Common::RBAC::EnforcementMode mode) const;

  bool
  perRuleStatsEnabled(const RoleBasedAccessControlRouteSpecificFilterConfig* route_local) const;

private:
  const Filters::Common::RBAC::RoleBasedAccessControlEngine*
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_.get()
                                                                    : shadow_engine_.get();
  }

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats stats_;
  const DynamicMetadataFieldNames enforced_field_names_;
  const DynamicMetadataFieldNames shadow_field_names_;
  const bool per_rule_stats_;

  ActionValidationVisitor action_validation_visitor_;
  std::unique_ptr<const Filters::Common::RBAC::RoleBasedAccessControlEngine> engine_;
  std::unique_ptr<const Filters::Common::RBAC::RoleBasedAccessControlEngine> shadow_engine_;
};

using RoleBasedAccessControlFilterConfigSharedPtr =
    std::shared_ptr<RoleBasedAccessControlFilterConfig>;

/**
 * A filter that provides role-based access control authorization for HTTP requests.
 */
class RoleBasedAccessControlFilter : public Http::StreamDecoderFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {
public:
  RoleBasedAccessControlFilter(RoleBasedAccessControlFilterConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override {}

protected:
  // Evaluates the shadow engine and then the enforced engine of the route's most specific config,
  // or of this filter config when the route has none, and writes both results to the dynamic
  // metadata under `metadata_namespace` once both have run.
  // Sends a local reply and returns StopIteration on denial, and returns Continue otherwise.
  Http::FilterHeadersStatus evaluateEngines(const Http::RequestHeaderMap& headers,
                                            absl::string_view metadata_namespace) const;

  RoleBasedAccessControlFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};

private:
  // The outcome of one engine. `result_` stays empty when the config has no such engine.
  struct EngineResult {
    absl::string_view result_;
    std::string effective_policy_id_;
  };

  // Handles shadow engine evaluation and updates stats.
  void evaluateShadowEngine(const Http::RequestHeaderMap& headers,
                            const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
                            EngineResult& shadow) const;

  // Handles enforced engine evaluation and updates stats. Sends a local reply on denial and
  // returns StopIteration; returns Continue otherwise.
  Http::FilterHeadersStatus
  evaluateEnforcedEngine(const Http::RequestHeaderMap& headers,
                         const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
                         EngineResult& enforced) const;
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
