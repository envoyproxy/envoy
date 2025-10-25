#pragma once

#include "envoy/router/router.h"

#include "source/common/http/hash_policy.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/upstream/retry_factory.h"
#include "source/extensions/early_data/default_early_data_policy.h"

namespace Envoy {
namespace Http {

struct NullHedgePolicy : public Router::HedgePolicy {
  // Router::HedgePolicy
  uint32_t initialRequests() const override { return 1; }
  const envoy::type::v3::FractionalPercent& additionalRequestChance() const override {
    return additional_request_chance_;
  }
  bool hedgeOnPerTryTimeout() const override { return false; }

  const envoy::type::v3::FractionalPercent additional_request_chance_;
};

struct NullRateLimitPolicy : public Router::RateLimitPolicy {
  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
  getApplicableRateLimit(uint64_t) const override {
    return rate_limit_policy_entry_;
  }
  bool empty() const override { return true; }

  static const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
      rate_limit_policy_entry_;
};

struct NullCommonConfig : public Router::CommonConfig {
  const std::vector<LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::string& name() const override { return EMPTY_STRING; }
  bool usesVhds() const override { return false; }
  bool mostSpecificHeaderMutationsWins() const override { return false; }
  uint32_t maxDirectResponseBodySizeBytes() const override { return 0; }
  const envoy::config::core::v3::Metadata& metadata() const override {
    return Router::DefaultRouteMetadataPack::get().proto_metadata_;
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return Router::DefaultRouteMetadataPack::get().typed_metadata_;
  }

  static const std::vector<LowerCaseString> internal_only_headers_;
};

struct NullVirtualHost : public Router::VirtualHost {
  // Router::VirtualHost
  const std::string& name() const override { return EMPTY_STRING; }
  Stats::StatName statName() const override { return {}; }
  const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
  const Router::CommonConfig& routeConfig() const override { return route_configuration_; }
  bool includeAttemptCountInRequest() const override { return false; }
  bool includeAttemptCountInResponse() const override { return false; }
  bool includeIsTimeoutRetryHeader() const override { return false; }
  uint64_t requestBodyBufferLimit() const override { return std::numeric_limits<uint64_t>::max(); }
  const Router::RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view) const override {
    return nullptr;
  }
  Router::RouteSpecificFilterConfigs perFilterConfigs(absl::string_view) const override {
    return {};
  }
  const envoy::config::core::v3::Metadata& metadata() const override {
    return Router::DefaultRouteMetadataPack::get().proto_metadata_;
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return Router::DefaultRouteMetadataPack::get().typed_metadata_;
  }
  const Router::VirtualCluster* virtualCluster(const Http::HeaderMap&) const override {
    return nullptr;
  }

  static const NullRateLimitPolicy rate_limit_policy_;
  static const NullCommonConfig route_configuration_;
};

struct NullPathMatchCriterion : public Router::PathMatchCriterion {
  Router::PathMatchType matchType() const override { return Router::PathMatchType::None; }
  const std::string& matcher() const override { return EMPTY_STRING; }
};

struct RouteEntryImpl : public Router::RouteEntry {
  static absl::StatusOr<std::unique_ptr<RouteEntryImpl>>
  create(const std::string& cluster_name, const absl::optional<std::chrono::milliseconds>& timeout,
         const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
             hash_policy,
         Router::RetryPolicyConstSharedPtr retry_policy, Regex::Engine& regex_engine,
         const Router::MetadataMatchCriteria* metadata_match) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::unique_ptr<RouteEntryImpl>(
        new RouteEntryImpl(cluster_name, timeout, hash_policy, std::move(retry_policy),
                           regex_engine, creation_status, metadata_match));
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

protected:
  RouteEntryImpl(
      const std::string& cluster_name, const absl::optional<std::chrono::milliseconds>& timeout,
      const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
          hash_policy,
      Router::RetryPolicyConstSharedPtr retry_policy, Regex::Engine& regex_engine,
      absl::Status& creation_status, const Router::MetadataMatchCriteria* metadata_match)
      : metadata_match_(metadata_match), retry_policy_(std::move(retry_policy)),
        cluster_name_(cluster_name), timeout_(timeout) {
    if (!hash_policy.empty()) {
      auto policy_or_error = HashPolicyImpl::create(hash_policy, regex_engine);
      SET_AND_RETURN_IF_NOT_OK(policy_or_error.status(), creation_status);
      hash_policy_ = std::move(*policy_or_error);
    }
  }

  // Router::RouteEntry
  const std::string& clusterName() const override { return cluster_name_; }
  const Router::RouteStatsContextOptRef routeStatsContext() const override {
    return Router::RouteStatsContextOptRef();
  }
  Http::Code clusterNotFoundResponseCode() const override {
    return Http::Code::InternalServerError;
  }
  const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
  std::string currentUrlPathAfterRewrite(const Http::RequestHeaderMap&, const Formatter::Context&,
                                         const StreamInfo::StreamInfo&) const override {
    return {};
  }
  void finalizeRequestHeaders(Http::RequestHeaderMap&, const Formatter::HttpFormatterContext&,
                              const StreamInfo::StreamInfo&, bool) const override {}
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo&,
                                                 bool) const override {
    return {};
  }
  void finalizeResponseHeaders(Http::ResponseHeaderMap&, const Formatter::HttpFormatterContext&,
                               const StreamInfo::StreamInfo&) const override {}
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo&,
                                                  bool) const override {
    return {};
  }
  const HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
  const Router::HedgePolicy& hedgePolicy() const override { return hedge_policy_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_;
  }
  Upstream::ResourcePriority priority() const override {
    return Upstream::ResourcePriority::Default;
  }
  const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const Router::RetryPolicyConstSharedPtr& retryPolicy() const override { return retry_policy_; }
  const Router::InternalRedirectPolicy& internalRedirectPolicy() const override {
    return internal_redirect_policy_;
  }
  const Router::PathMatcherSharedPtr& pathMatcher() const override { return path_matcher_; }
  const Router::PathRewriterSharedPtr& pathRewriter() const override { return path_rewriter_; }
  uint64_t requestBodyBufferLimit() const override { return std::numeric_limits<uint64_t>::max(); }
  const std::vector<Router::ShadowPolicyPtr>& shadowPolicies() const override {
    return shadow_policies_;
  }
  std::chrono::milliseconds timeout() const override {
    if (timeout_) {
      return timeout_.value();
    } else {
      return std::chrono::milliseconds(0);
    }
  }
  bool usingNewTimeouts() const override { return false; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return absl::nullopt; }
  absl::optional<std::chrono::milliseconds> flushTimeout() const override { return absl::nullopt; }
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return absl::nullopt;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override {
    return absl::nullopt;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override {
    return absl::nullopt;
  }
  absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
    return absl::nullopt;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override {
    return absl::nullopt;
  }
  const Router::TlsContextMatchCriteria* tlsContextMatchCriteria() const override {
    return nullptr;
  }
  const std::multimap<std::string, std::string>& opaqueConfig() const override {
    return opaque_config_;
  }
  bool autoHostRewrite() const override { return false; }
  bool appendXfh() const override { return false; }
  bool includeVirtualHostRateLimits() const override { return true; }
  const Router::PathMatchCriterion& pathMatchCriterion() const override {
    return path_match_criterion_;
  }

  const ConnectConfigOptRef connectConfig() const override { return connect_config_nullopt_; }

  bool includeAttemptCountInRequest() const override { return false; }
  bool includeAttemptCountInResponse() const override { return false; }
  const Router::RouteEntry::UpgradeMap& upgradeMap() const override { return upgrade_map_; }
  const Router::EarlyDataPolicy& earlyDataPolicy() const override { return *early_data_policy_; }
  void refreshRouteCluster(const Http::RequestHeaderMap&,
                           const StreamInfo::StreamInfo&) const override {}

  const Router::MetadataMatchCriteria* metadata_match_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  const Router::RetryPolicyConstSharedPtr retry_policy_;

  static const NullHedgePolicy hedge_policy_;
  static const NullRateLimitPolicy rate_limit_policy_;
  static const Router::InternalRedirectPolicyImpl internal_redirect_policy_;
  static const Router::PathMatcherSharedPtr path_matcher_;
  static const Router::PathRewriterSharedPtr path_rewriter_;
  static const std::vector<Router::ShadowPolicyPtr> shadow_policies_;
  static const std::multimap<std::string, std::string> opaque_config_;
  static const NullPathMatchCriterion path_match_criterion_;

  Router::RouteEntry::UpgradeMap upgrade_map_;
  const std::string cluster_name_;
  absl::optional<std::chrono::milliseconds> timeout_;
  static const ConnectConfigOptRef connect_config_nullopt_;
  // Pass early data option config through StreamOptions.
  std::unique_ptr<Router::EarlyDataPolicy> early_data_policy_{
      new Router::DefaultEarlyDataPolicy(true)};
};

struct NullRouteImpl : public Router::Route {
  static absl::StatusOr<std::unique_ptr<NullRouteImpl>>
  create(const std::string cluster_name, Router::RetryPolicyConstSharedPtr retry_policy,
         Regex::Engine& regex_engine, const absl::optional<std::chrono::milliseconds>& timeout = {},
         const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
             hash_policy = {},
         const Router::MetadataMatchCriteria* metadata_match = nullptr) {
    absl::Status creation_status;
    auto ret = std::unique_ptr<NullRouteImpl>(
        new NullRouteImpl(cluster_name, std::move(retry_policy), regex_engine, timeout, hash_policy,
                          creation_status, metadata_match));
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  // Router::Route
  const Router::DirectResponseEntry* directResponseEntry() const override { return nullptr; }
  const Router::RouteEntry* routeEntry() const override { return route_entry_.get(); }
  const Router::Decorator* decorator() const override { return nullptr; }
  const Router::RouteTracing* tracingConfig() const override { return nullptr; }
  const Router::RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view) const override {
    return nullptr;
  }
  Router::RouteSpecificFilterConfigs perFilterConfigs(absl::string_view) const override {
    return {};
  }
  const envoy::config::core::v3::Metadata& metadata() const override {
    return Router::DefaultRouteMetadataPack::get().proto_metadata_;
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return Router::DefaultRouteMetadataPack::get().typed_metadata_;
  }
  absl::optional<bool> filterDisabled(absl::string_view) const override { return {}; }
  const std::string& routeName() const override { return EMPTY_STRING; }
  const Router::VirtualHostConstSharedPtr& virtualHost() const override { return virtual_host_; }

  std::unique_ptr<RouteEntryImpl> route_entry_;
  static const Router::VirtualHostConstSharedPtr virtual_host_;

protected:
  NullRouteImpl(const std::string cluster_name, Router::RetryPolicyConstSharedPtr retry_policy,
                Regex::Engine& regex_engine,
                const absl::optional<std::chrono::milliseconds>& timeout,
                const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
                    hash_policy,
                absl::Status& creation_status,
                const Router::MetadataMatchCriteria* metadata_match) {
    auto entry_or_error = RouteEntryImpl::create(
        cluster_name, timeout, hash_policy, std::move(retry_policy), regex_engine, metadata_match);
    SET_AND_RETURN_IF_NOT_OK(entry_or_error.status(), creation_status);
    route_entry_ = std::move(*entry_or_error);
  }
};

} // namespace Http
} // namespace Envoy
