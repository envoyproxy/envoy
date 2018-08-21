#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { Internal, External, Both };

/**
 * Global configuration for the HTTP rate limit filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::config::filter::http::rate_limit::v2::RateLimit& config,
               const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
               Runtime::Loader& runtime, Upstream::ClusterManager& cm)
      : domain_(config.domain()), stage_(static_cast<uint64_t>(config.stage())),
        request_type_(config.request_type().empty() ? stringToType("both")
                                                    : stringToType(config.request_type())),
        local_info_(local_info), scope_(scope), runtime_(runtime), cm_(cm),
        failure_mode_deny_(config.failure_mode_deny()) {}
  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  uint64_t stage() const { return stage_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Scope& scope() { return scope_; }
  Upstream::ClusterManager& cm() { return cm_; }
  FilterRequestType requestType() const { return request_type_; }

  bool failureModeAllow() const { return !failure_mode_deny_; }

private:
  static FilterRequestType stringToType(const std::string& request_type) {
    if (request_type == "internal") {
      return FilterRequestType::Internal;
    } else if (request_type == "external") {
      return FilterRequestType::External;
    } else {
      ASSERT(request_type == "both");
      return FilterRequestType::Both;
    }
  }

  const std::string domain_;
  const uint64_t stage_;
  const FilterRequestType request_type_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  const bool failure_mode_deny_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

/**
 * HTTP rate limit filter. Depending on the route configuration, this filter calls the global
 * rate limiting service before allowing further filter iteration.
 */
class Filter : public Http::StreamFilter, public RateLimit::RequestCallbacks {
public:
  Filter(FilterConfigSharedPtr config, RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(RateLimit::LimitStatus status, Http::HeaderMapPtr&& headers) override;

private:
  void initiateCall(const Http::HeaderMap& headers);
  void populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<RateLimit::Descriptor>& descriptors,
                                    const Router::RouteEntry* route_entry,
                                    const Http::HeaderMap& headers) const;
  void addHeaders(Http::HeaderMap& headers);

  enum class State { NotStarted, Calling, Complete, Responded };

  FilterConfigSharedPtr config_;
  RateLimit::ClientPtr client_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{};
  Http::HeaderMapPtr headers_to_add_;
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
