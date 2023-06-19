#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

/**
 * All local rate limit stats. @see stats_macros.h
 */
#define ALL_LOCAL_RATE_LIMIT_STATS(COUNTER)                                                        \
  COUNTER(enabled)                                                                                 \
  COUNTER(enforced)                                                                                \
  COUNTER(rate_limited)                                                                            \
  COUNTER(ok)

/**
 * Struct definition for all local rate limit stats. @see stats_macros.h
 */
struct LocalRateLimitStats {
  ALL_LOCAL_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Type of virtual host rate limit options
 */
enum class VhRateLimitOptions { Override, Include, Ignore };

class PerConnectionRateLimiter : public StreamInfo::FilterState::Object {
public:
  PerConnectionRateLimiter(
      const std::chrono::milliseconds& fill_interval, uint32_t max_tokens, uint32_t tokens_per_fill,
      Envoy::Event::Dispatcher& dispatcher,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptor)
      : rate_limiter_(fill_interval, max_tokens, tokens_per_fill, dispatcher, descriptor) {}
  static const std::string& key();
  const Filters::Common::LocalRateLimit::LocalRateLimiterImpl& value() const {
    return rate_limiter_;
  }

private:
  Filters::Common::LocalRateLimit::LocalRateLimiterImpl rate_limiter_;
};

/**
 * Global configuration for the HTTP local rate limit filter.
 */
class FilterConfig : public Router::RouteSpecificFilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
               const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
               Stats::Scope& scope, Runtime::Loader& runtime, bool per_route = false);
  ~FilterConfig() override {
    // Ensure that the LocalRateLimiterImpl instance will be destroyed on the thread where its inner
    // timer is created and running.
    auto shared_ptr_wrapper =
        std::make_shared<std::unique_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>>(
            std::move(rate_limiter_));
    dispatcher_.post([shared_ptr_wrapper]() { shared_ptr_wrapper->reset(); });
  }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  Runtime::Loader& runtime() { return runtime_; }
  bool requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  uint32_t maxTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  uint32_t remainingTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  int64_t
  remainingFillInterval(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  bool enabled() const;
  bool enforced() const;
  LocalRateLimitStats& stats() const { return stats_; }
  const Router::HeaderParser& responseHeadersParser() const { return *response_headers_parser_; }
  const Router::HeaderParser& requestHeadersParser() const { return *request_headers_parser_; }
  Http::Code status() const { return status_; }
  uint64_t stage() const { return stage_; }
  bool hasDescriptors() const { return has_descriptors_; }
  const std::chrono::milliseconds& fillInterval() const { return fill_interval_; }
  uint32_t maxTokens() const { return max_tokens_; }
  uint32_t tokensPerFill() const { return tokens_per_fill_; }
  const Protobuf::RepeatedPtrField<
      envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>&
  descriptors() const {
    return descriptors_;
  }
  bool rateLimitPerConnection() const { return rate_limit_per_connection_; }
  bool enableXRateLimitHeaders() const { return enable_x_rate_limit_headers_; }
  envoy::extensions::common::ratelimit::v3::VhRateLimitsOptions virtualHostRateLimits() const {
    return vh_rate_limits_;
  }

private:
  friend class FilterTest;

  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::BadRequest) {
      return code;
    }
    return Http::Code::TooManyRequests;
  }

  Event::Dispatcher& dispatcher_;
  const Http::Code status_;
  mutable LocalRateLimitStats stats_;
  const std::chrono::milliseconds fill_interval_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const Protobuf::RepeatedPtrField<
      envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>
      descriptors_;
  const bool rate_limit_per_connection_;
  std::unique_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl> rate_limiter_;
  const LocalInfo::LocalInfo& local_info_;
  Runtime::Loader& runtime_;
  const absl::optional<Envoy::Runtime::FractionalPercent> filter_enabled_;
  const absl::optional<Envoy::Runtime::FractionalPercent> filter_enforced_;
  Router::HeaderParserPtr response_headers_parser_;
  Router::HeaderParserPtr request_headers_parser_;
  const uint64_t stage_;
  const bool has_descriptors_;
  const bool enable_x_rate_limit_headers_;
  const envoy::extensions::common::ratelimit::v3::VhRateLimitsOptions vh_rate_limits_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP local rate limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(FilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

private:
  friend class FilterTest;

  void populateDescriptors(std::vector<RateLimit::LocalDescriptor>& descriptors,
                           Http::RequestHeaderMap& headers);
  void populateDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                           std::vector<RateLimit::LocalDescriptor>& descriptors,
                           Http::RequestHeaderMap& headers);
  VhRateLimitOptions getVirtualHostRateLimitOption(const Router::RouteConstSharedPtr& route);
  const Filters::Common::LocalRateLimit::LocalRateLimiterImpl& getPerConnectionRateLimiter();
  bool requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors);
  uint32_t maxTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors);
  uint32_t remainingTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors);
  int64_t remainingFillInterval(absl::Span<const RateLimit::LocalDescriptor> request_descriptors);

  const FilterConfig* getConfig() const;
  FilterConfigSharedPtr config_;

  absl::optional<std::vector<RateLimit::LocalDescriptor>> stored_descriptors_;
  VhRateLimitOptions vh_rate_limits_;
};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
