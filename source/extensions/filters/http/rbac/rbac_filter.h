#pragma once

#include <memory>

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"
#include "source/extensions/filters/common/rbac/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

class ActionValidationVisitor : public Filters::Common::RBAC::ActionValidationVisitor {
public:
  absl::Status performDataInputValidation(
      const Envoy::Matcher::DataInputFactory<Http::HttpMatchingData>& data_input,
      absl::string_view type_url) override;
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

private:
  ActionValidationVisitor action_validation_visitor_;
  std::unique_ptr<Filters::Common::RBAC::RoleBasedAccessControlEngine> engine_;
  std::unique_ptr<Filters::Common::RBAC::RoleBasedAccessControlEngine> shadow_engine_;
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
  std::string shadowEffectivePolicyIdField() const {
    return shadow_rules_stat_prefix_ +
           Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEffectivePolicyIdField;
  }
  std::string shadowEngineResultField() const {
    return shadow_rules_stat_prefix_ +
           Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEngineResultField;
  }

  const Filters::Common::RBAC::RoleBasedAccessControlEngine*
  engine(const Http::StreamFilterCallbacks* callbacks,
         Filters::Common::RBAC::EnforcementMode mode) const;

private:
  const Filters::Common::RBAC::RoleBasedAccessControlEngine*
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_.get()
                                                                    : shadow_engine_.get();
  }

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats stats_;
  const std::string shadow_rules_stat_prefix_;

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
      : config_(config) {}

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

private:
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
