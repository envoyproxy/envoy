#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/http/context.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/common/ratelimit/stat_names.h"

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
  FilterConfig(const envoy::extensions::filters::http::ratelimit::v3::RateLimit& config,
               const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
               Runtime::Loader& runtime, Http::Context& http_context)
      : domain_(config.domain()), stage_(static_cast<uint64_t>(config.stage())),
        request_type_(config.request_type().empty() ? stringToType("both")
                                                    : stringToType(config.request_type())),
        local_info_(local_info), scope_(scope), runtime_(runtime),
        failure_mode_deny_(config.failure_mode_deny()),
        rate_limited_grpc_status_(
            config.rate_limited_as_resource_exhausted()
                ? absl::make_optional(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted)
                : absl::nullopt),
        http_context_(http_context), stat_names_(scope.symbolTable()) {}
  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  uint64_t stage() const { return stage_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Scope& scope() { return scope_; }
  FilterRequestType requestType() const { return request_type_; }
  bool failureModeAllow() const { return !failure_mode_deny_; }
  const absl::optional<Grpc::Status::GrpcStatus> rateLimitedGrpcStatus() const {
    return rate_limited_grpc_status_;
  }
  Http::Context& httpContext() { return http_context_; }
  Filters::Common::RateLimit::StatNames& statNames() { return stat_names_; }

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
  const bool failure_mode_deny_;
  const absl::optional<Grpc::Status::GrpcStatus> rate_limited_grpc_status_;
  Http::Context& http_context_;
  Filters::Common::RateLimit::StatNames stat_names_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP rate limit filter. Depending on the route configuration, this filter calls the global
 * rate limiting service before allowing further filter iteration.
 */
class Filter : public Http::StreamFilter, public Filters::Common::RateLimit::RequestCallbacks {
public:
  Filter(FilterConfigSharedPtr config, Filters::Common::RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(Filters::Common::RateLimit::LimitStatus status,
                Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add) override;

private:
  void initiateCall(const Http::RequestHeaderMap& headers);
  void populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                                    const Router::RouteEntry* route_entry,
                                    const Http::HeaderMap& headers) const;
  void populateResponseHeaders(Http::HeaderMap& response_headers);
  void appendRequestHeaders(Http::HeaderMapPtr& request_headers_to_add);

  Http::Context& httpContext() { return config_->httpContext(); }

  enum class State { NotStarted, Calling, Complete, Responded };

  FilterConfigSharedPtr config_;
  Filters::Common::RateLimit::ClientPtr client_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{};
  Http::ResponseHeaderMapPtr response_headers_to_add_;
  Http::RequestHeaderMap* request_headers_{};
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
