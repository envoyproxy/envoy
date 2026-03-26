#pragma once

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/router/router.h"

#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"
#include "source/extensions/filters/http/bandwidth_share/token_bucket_singleton.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

using HttpMatchTreePtr = Matcher::MatchTreePtr<Http::HttpMatchingData>;

class FilterConfig : public Router::RouteSpecificFilterConfig {
public:
  struct TenantConfig {
    uint32_t weight_;
    bool include_stats_tag_;
  };
  struct ResponseTrailers {
    ResponseTrailers(absl::string_view prefix);
    Http::LowerCaseString request_duration_;
    Http::LowerCaseString response_duration_;
    Http::LowerCaseString request_delay_;
    Http::LowerCaseString response_delay_;
  };
  class SharedStats {
  public:
    SharedStats(const absl::string_view bucket_id, const BandwidthShareStatNames& stat_names,
                Stats::Scope& scope, bool is_response)
        : bucket_id_(bucket_id), stat_names_(stat_names), stats_scope_(scope),
          is_response_(is_response) {}
    BandwidthShareStats& forTenant(absl::string_view tenant);

  private:
    Thread::MutexBasicLockable mu_;
    const absl::string_view bucket_id_;
    const BandwidthShareStatNames& stat_names_;
    Stats::Scope& stats_scope_;
    const bool is_response_;
    absl::node_hash_map<std::string, BandwidthShareStats> stats_by_tenant_ ABSL_GUARDED_BY(mu_);
  };

  /**
   * @param context provides time source, stats scope etc.
   * @param bucket_singleton provides token buckets on-demand - may update between or
   * during requests.
   * @param request_bucket_id identifies the bucket to use for requests, or if nullopt, don't limit
   * requests.
   * @param response_bucket_id identifies the bucket to use for responses, or if nullopt, don't
   * limit responses.
   * @param response_trailer_prefix is the prefix to add to response trailers, which may be an empty
   * string. If nullopt, don't add response trailers.
   * @param tenant_name_selector results in an action that outputs a tenant name, which may be based
   * on the stream info.
   * @param tenant_configs provides additional config for specific named tenants.
   * @param default_tenant_config provides additional config for tenants not named in
   * tenant_configs.
   */
  FilterConfig(Server::Configuration::ServerFactoryContext& context,
               std::shared_ptr<TokenBucketSingleton> bucket_singleton,
               absl::optional<absl::string_view> request_bucket_id,
               absl::optional<absl::string_view> response_bucket_id,
               absl::optional<absl::string_view> response_trailer_prefix,
               HttpMatchTreePtr tenant_name_selector,
               absl::flat_hash_map<std::string, TenantConfig>&& tenant_configs,
               TenantConfig default_tenant_config);

  bool enabled() const { return request_bucket_id_.has_value() || response_bucket_id_.has_value(); }
  std::shared_ptr<FairTokenBucket::Client> getRequestBucket(absl::string_view tenant) const;
  std::shared_ptr<FairTokenBucket::Client> getResponseBucket(absl::string_view tenant) const;
  std::string getTenantName(const StreamInfo::StreamInfo& stream_info,
                            const Http::RequestHeaderMap& request_headers) const;
  const TenantConfig& getTenantConfig(absl::string_view tenant) const;
  absl::string_view tenantForStats(absl::string_view tenant) const;
  BandwidthShareStats& requestStatsForTenant(absl::string_view tenant) const;
  BandwidthShareStats& responseStatsForTenant(absl::string_view tenant) const;
  bool enableResponseTrailers() const { return response_trailers_.has_value(); }
  const ResponseTrailers& responseTrailers() const {
    ASSERT(response_trailers_.has_value());
    return *response_trailers_;
  }
  TimeSource& timeSource() const { return time_source_; }

private:
  std::shared_ptr<FairTokenBucket::Client> getBucketById(const absl::optional<std::string>& id,
                                                         absl::string_view tenant) const;
  TimeSource& time_source_;
  const std::shared_ptr<TokenBucketSingleton> bucket_singleton_;
  const absl::optional<std::string> request_bucket_id_;
  const absl::optional<std::string> response_bucket_id_;
  // Unset means don't do response trailers.
  const absl::optional<ResponseTrailers> response_trailers_;
  const HttpMatchTreePtr tenant_name_selector_;
  const absl::flat_hash_map<std::string, TenantConfig> tenant_configs_;
  const TenantConfig default_tenant_config_;
  const std::unique_ptr<SharedStats> request_stats_;
  const std::unique_ptr<SharedStats> response_stats_;
};

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
