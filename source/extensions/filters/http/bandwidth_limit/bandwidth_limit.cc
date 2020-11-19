#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/http/utility.h"
#include "bandwidth_limit.h"

using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;
using envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit::EnableMode;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& config,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
    TimeSource& time_source, const bool per_route)
    : stats_(generateStats(config.stat_prefix(), scope)), runtime_(runtime),
      scope_(scope), time_source_(time_source),
      limit_kbps_(config.has_limit_kbps() ? absl::optional<uint64_t>(config.limit_kbps())
                                          : absl::nullopt),
      enable_mode_(config.has_enable_mode() ? config.enable_mode() : EnableMode::Disabled)
      enforce_threshold_Kbps_(config.has_enforce_threshold_kbps()
                                  ? absl::optional<std::string>(config.enforce_threshold_kbps())
                                  : absl::nullopt),
      fill_rate_(config.has_fill_rate() ? config.fill_rate() : StreamRateLimiter::DefaultFillRate) {
  // Note: no limit is fine for the global config, which would be the case for enabling
  //       the filter globally but disabled and then applying limits at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an no limit
  //       so we throw an error. If there's no limit configured globally or at the vhost/route
  //       level, no rate limiting is applied.
  if (per_route && !config.has_limit_kbps()) {
    throw EnvoyException("bandwidth filter limit must be set for per filter configs");
  }

  // The token bucket is configured with a max token count of the number of ticks per second,
  // and refills at the same rate, so that we have a per second limit which refills gradually in
  // 1/fill_rate intervals.
  token_bucket_ = std::make_shared<TokenBucketImpl>(fill_rate_, time_source, fill_rate_),
}

BandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_bandwidth_limit";
  return {ALL_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

EnableMode FilterConfig::enable_mode() const { return enable_mode_; }

// BandwidthFilter members

Http::FilterHeadersStatus BandwidthFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto* config = getConfig();

  auto mode = config->enable_mode();
  if (mode == EnableMode::Disabled) {
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().enabled_.inc();

  if (mode & EnableMode::Ingress) {
    downstream_limiter_ =
        std::make_unique<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter>(
            config_->limit(), decoder_callbacks_->decoderBufferLimit(),
            [this] { decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark(); },
            [this] { decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark(); },
            [this](Buffer::Instance& data, bool end_stream) {
              decoder_callbacks_->injectDecodedDataToFilterChain(data, end_stream);
            },
            [this] { decoder_callbacks_->continueDecoding(); }, config_->timeSource(),
            decoder_callbacks_->dispatcher(), decoder_callbacks_->scope(), config_->tokenBucket(),
            config_->fill_rate());
  }

  if (mode & EnableMode::Egress) {
    upstream_limiter_ = std::make_unique<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter>(
        config_->limit(), encoder_callbacks_->encoderBufferLimit(),
        [this] { encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark(); },
        [this] { encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
        },
        [this] { encoder_callbacks_->continueEncoding(); }, config_->timeSource(),
        decoder_callbacks_->dispatcher(), decoder_callbacks_->scope(), config_->tokenBucket(),
        config_->fill_rate());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (downstream_limiter_ != nullptr) {
    downstream_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (downstream_limiter_ != nullptr) {
    return downstream_limiter_->onTrailers() ? Http::FilterTrailersStatus::StopIteration
                                             : Http::FilterTrailersStatus::Continue;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterDataStatus BandwidthFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (upstream_limiter_ != nullptr) {
    upstream_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (upstream_limiter_ != nullptr) {
    return upstream_limiter_->onTrailers() ? Http::FilterTrailersStatus::StopIteration
                                           : Http::FilterTrailersStatus::Continue;
  }
  return Http::FilterDataStatus::Continue;
}

const FilterConfig* BandwidthFilter::getConfig() const {
  const auto* config = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      "envoy.filters.http.bandwidth_limit", decoder_callbacks_->route());
  if (config) {
    return config;
  }

  return config_.get();
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
