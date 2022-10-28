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

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/common/ratelimit/stat_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { Internal, External, Both };

/**
 * Type of virtual host rate limit options
 */
enum class VhRateLimitOptions { Override, Include, Ignore };

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
        enable_x_ratelimit_headers_(
            config.enable_x_ratelimit_headers() ==
            envoy::extensions::filters::http::ratelimit::v3::RateLimit::DRAFT_VERSION_03),
        disable_x_envoy_ratelimited_header_(config.disable_x_envoy_ratelimited_header()),
        rate_limited_grpc_status_(
            config.rate_limited_as_resource_exhausted()
                ? absl::make_optional(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted)
                : absl::nullopt),
        http_context_(http_context), stat_names_(scope.symbolTable()),
        rate_limited_status_(toErrorCode(config.rate_limited_status().code())),
        response_headers_parser_(
            Envoy::Router::HeaderParser::configure(config.response_headers_to_add())) {}
  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  uint64_t stage() const { return stage_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Scope& scope() { return scope_; }
  FilterRequestType requestType() const { return request_type_; }
  bool failureModeAllow() const { return !failure_mode_deny_; }
  bool enableXRateLimitHeaders() const { return enable_x_ratelimit_headers_; }
  bool enableXEnvoyRateLimitedHeader() const { return !disable_x_envoy_ratelimited_header_; }
  const absl::optional<Grpc::Status::GrpcStatus> rateLimitedGrpcStatus() const {
    return rate_limited_grpc_status_;
  }
  Http::Context& httpContext() { return http_context_; }
  Filters::Common::RateLimit::StatNames& statNames() { return stat_names_; }
  Http::Code rateLimitedStatus() { return rate_limited_status_; }
  const Router::HeaderParser& responseHeadersParser() const { return *response_headers_parser_; }

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

  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::BadRequest) {
      return code;
    }
    return Http::Code::TooManyRequests;
  }

  const std::string domain_;
  const uint64_t stage_;
  const FilterRequestType request_type_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  const bool failure_mode_deny_;
  const bool enable_x_ratelimit_headers_;
  const bool disable_x_envoy_ratelimited_header_;
  const absl::optional<Grpc::Status::GrpcStatus> rate_limited_grpc_status_;
  Http::Context& http_context_;
  Filters::Common::RateLimit::StatNames stat_names_;
  const Http::Code rate_limited_status_;
  Router::HeaderParserPtr response_headers_parser_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute& config)
      : vh_rate_limits_(config.vh_rate_limits()) {}

  envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute::VhRateLimitsOptions
  virtualHostRateLimits() const {
    return vh_rate_limits_;
  }

private:
  const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute::VhRateLimitsOptions
      vh_rate_limits_;
};

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
  Http::FilterHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(Filters::Common::RateLimit::LimitStatus status,
                Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses,
                Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add,
                const std::string& response_body,
                Filters::Common::RateLimit::DynamicMetadataPtr&& dynamic_metadata) override;

private:
  void initiateCall(const Http::RequestHeaderMap& headers);
  void populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                                    const Http::RequestHeaderMap& headers) const;
  void populateResponseHeaders(Http::HeaderMap& response_headers, bool from_local_reply);
  void appendRequestHeaders(Http::HeaderMapPtr& request_headers_to_add);
  VhRateLimitOptions getVirtualHostRateLimitOption(const Router::RouteConstSharedPtr& route);

  Http::Context& httpContext() { return config_->httpContext(); }

  enum class State { NotStarted, Calling, Complete, Responded };

  FilterConfigSharedPtr config_;
  Filters::Common::RateLimit::ClientPtr client_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  VhRateLimitOptions vh_rate_limits_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{};
  Http::ResponseHeaderMapPtr response_headers_to_add_;
  Http::RequestHeaderMap* request_headers_{};
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
