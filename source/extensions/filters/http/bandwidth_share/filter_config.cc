#include "source/extensions/filters/http/bandwidth_share/filter_config.h"

#include <utility>

#include "envoy/server/factory_context.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/actions/string_returning_action.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

using Matcher::Actions::StringReturningAction;

namespace {

template <class MakeInit> class LazyInit {
public:
  using Value = decltype(std::declval<MakeInit>()());

  explicit LazyInit(MakeInit make_init) : make_init_(std::move(make_init)) {}
  operator Value() const { return make_init_(); }

private:
  MakeInit make_init_;
};

template <class MakeInit> LazyInit<MakeInit> lazyInit(MakeInit make_init) {
  return LazyInit<MakeInit>(std::move(make_init));
}

} // namespace

class FilterConfig::SharedStats::ThreadLocalStatsStore : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalStatsStore(absl::string_view bucket_id,
                        std::shared_ptr<TokenBucketSingleton> bucket_singleton,
                        Stats::Scope& stats_scope, bool is_response)
      : dynamic_pool_(stats_scope.symbolTable()), bucket_id_(dynamic_pool_.add(bucket_id)),
        bucket_singleton_(std::move(bucket_singleton)), stats_scope_(stats_scope),
        is_response_(is_response) {}

  BandwidthShareStats& forTenant(absl::string_view tenant) {
    auto [it, _] = stats_by_tenant_.try_emplace(
        tenant, bucket_singleton_->stat_names_, stats_scope_, bucket_id_,
        lazyInit([this, tenant] { return dynamic_pool_.add(tenant); }), is_response_);
    return it->second;
  }

private:
  Stats::StatNameDynamicPool dynamic_pool_;
  const Stats::StatName bucket_id_;
  const std::shared_ptr<TokenBucketSingleton> bucket_singleton_;
  Stats::Scope& stats_scope_;
  const bool is_response_;
  absl::node_hash_map<std::string, BandwidthShareStats> stats_by_tenant_;
};

FilterConfig::SharedStats::SharedStats(absl::string_view bucket_id,
                                       std::shared_ptr<TokenBucketSingleton> bucket_singleton,
                                       Stats::Scope& scope, bool is_response,
                                       ThreadLocal::SlotAllocator& tls)
    : tls_(tls) {
  tls_.set([bucket_id = std::string(bucket_id), bucket_singleton = std::move(bucket_singleton),
            stats_scope = &scope, is_response](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalStatsStore>(bucket_id, bucket_singleton, *stats_scope,
                                                   is_response);
  });
}

BandwidthShareStats& FilterConfig::SharedStats::forTenant(absl::string_view tenant) {
  return tls_->forTenant(tenant);
}

absl::string_view FilterConfig::tenantForStats(absl::string_view tenant) const {
  const TenantConfig& tc = getTenantConfig(tenant);
  return tc.include_stats_tag_ ? tenant : "";
}

BandwidthShareStats& FilterConfig::requestStatsForTenant(absl::string_view tenant) const {
  ASSERT(request_stats_);
  return request_stats_->forTenant(tenantForStats(tenant));
}
BandwidthShareStats& FilterConfig::responseStatsForTenant(absl::string_view tenant) const {
  ASSERT(response_stats_);
  return response_stats_->forTenant(tenantForStats(tenant));
}

FilterConfig::ResponseTrailers::ResponseTrailers(absl::string_view prefix)
    : request_duration_(absl::StrCat(prefix, "bandwidth-request-duration-ms")),
      response_duration_(absl::StrCat(prefix, "bandwidth-response-duration-ms")),
      request_delay_(absl::StrCat(prefix, "bandwidth-request-delay-ms")),
      response_delay_(absl::StrCat(prefix, "bandwidth-response-delay-ms")) {}

std::string FilterConfig::getTenantName(const StreamInfo::StreamInfo& stream_info,
                                        const Http::RequestHeaderMap& request_headers) const {
  Http::Matching::HttpMatchingDataImpl data(stream_info);
  data.onRequestHeaders(request_headers);
  auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*tenant_name_selector_, data);
  if (!match_result.isMatch() || match_result.action() == nullptr) {
    return "";
  }
  const auto& action = match_result.action()->getTyped<StringReturningAction>();
  return action.getOutputString(stream_info);
}

FilterConfig::FilterConfig(Server::Configuration::ServerFactoryContext& context,
                           std::shared_ptr<TokenBucketSingleton> bucket_singleton,
                           std::optional<absl::string_view> request_bucket_id,
                           std::optional<absl::string_view> response_bucket_id,
                           std::optional<absl::string_view> response_trailer_prefix,
                           HttpMatchTreePtr tenant_name_selector,
                           absl::flat_hash_map<std::string, TenantConfig>&& tenant_configs,
                           TenantConfig default_tenant_config)
    : time_source_(context.timeSource()), bucket_singleton_(std::move(bucket_singleton)),
      request_bucket_id_(std::move(request_bucket_id)),
      response_bucket_id_(std::move(response_bucket_id)),
      response_trailers_(response_trailer_prefix
                             ? std::optional<ResponseTrailers>(response_trailer_prefix.value())
                             : std::nullopt),
      tenant_name_selector_(std::move(tenant_name_selector)),
      tenant_configs_(std::move(tenant_configs)),
      default_tenant_config_(std::move(default_tenant_config)),
      request_stats_(request_bucket_id_ ? std::make_unique<SharedStats>(
                                              *request_bucket_id_, bucket_singleton_,
                                              context.scope(), false, context.threadLocal())
                                        : nullptr),
      response_stats_(response_bucket_id_ ? std::make_unique<SharedStats>(
                                                *response_bucket_id_, bucket_singleton_,
                                                context.scope(), true, context.threadLocal())
                                          : nullptr) {}

std::shared_ptr<FairTokenBucket::Client>
FilterConfig::getBucketById(const std::optional<std::string>& id, absl::string_view tenant) const {
  if (!id) {
    return nullptr;
  }
  std::shared_ptr<FairTokenBucket::Bucket> bucket = bucket_singleton_->getBucket(*id);
  if (!bucket) {
    return nullptr;
  }
  return std::make_shared<FairTokenBucket::Client>(std::move(bucket), tenant,
                                                   getTenantConfig(tenant).weight_);
}

std::shared_ptr<FairTokenBucket::Client>
FilterConfig::getRequestBucket(absl::string_view tenant) const {
  return getBucketById(request_bucket_id_, tenant);
}

std::shared_ptr<FairTokenBucket::Client>
FilterConfig::getResponseBucket(absl::string_view tenant) const {
  return getBucketById(response_bucket_id_, tenant);
}

const FilterConfig::TenantConfig& FilterConfig::getTenantConfig(absl::string_view tenant) const {
  auto it = tenant_configs_.find(tenant);
  if (it != tenant_configs_.end()) {
    return it->second;
  }
  return default_tenant_config_;
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
