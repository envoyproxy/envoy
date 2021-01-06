#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/http/utility.h"

using envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit;
using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

FilterConfig::FilterConfig(const BandwidthLimit& config, Stats::Scope& scope,
                           Runtime::Loader& runtime, TimeSource& time_source, bool per_route)
    : stats_(generateStats(config.stat_prefix(), scope)), runtime_(runtime), scope_(scope),
      time_source_(time_source),
      limit_kbps_(config.has_limit_kbps() ? config.limit_kbps().value() : 0),
      enable_mode_(config.enable_mode()),
      fill_rate_(config.has_fill_rate() ? config.fill_rate().value()
                                        : StreamRateLimiter::DefaultFillRate) {
  if (per_route && !config.has_limit_kbps()) {
    throw EnvoyException("bandwidthlimitfilter: limit must be set for per route filter config");
  }

  if (fill_rate_ > MaxFillRate) {
    throw EnvoyException("bandwidthlimitfilter: fill rate must be <= 32");
  }
  // The token bucket is configured with a max token count of the number of ticks per second,
  // and refills at the same rate, so that we have a per second limit which refills gradually in
  // 1/fill_rate intervals.
  token_bucket_ = std::make_shared<TokenBucketImpl>(fill_rate_, time_source, fill_rate_, &mutex_);
}

BandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_bandwidth_limit";
  return {ALL_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

// BandwidthLimiter members

Http::FilterHeadersStatus BandwidthLimiter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto* config = getConfig();

  auto mode = config->enable_mode();
  ENVOY_LOG(trace, "BandwidthLimiter: decode headers: mode={}", static_cast<uint32_t>(mode));

  if (mode & BandwidthLimit::Ingress) {
    config->stats().enabled_.inc();
    ingress_limiter_ = std::make_unique<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter>(
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
  ENVOY_LOG(trace, "BandwidthLimiter: decode headers: ingress_limiter_={}",
            ingress_limiter_ ? true : false);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (ingress_limiter_ != nullptr) {
    ingress_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(trace, "BandwidthLimiter: decode data: ingress_limiter_ not set");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthLimiter::decodeTrailers(Http::RequestTrailerMap&) {
  if (ingress_limiter_ != nullptr) {
    return ingress_limiter_->onTrailers() ? Http::FilterTrailersStatus::StopIteration
                                          : Http::FilterTrailersStatus::Continue;
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus BandwidthLimiter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  const auto* config = getConfig();

  auto mode = config->enable_mode();
  ENVOY_LOG(trace, "BandwidthLimiter: encode headers: mode={}", static_cast<uint32_t>(mode));

  if (mode & BandwidthLimit::Egress) {
    config->stats().enabled_.inc();

    egress_limiter_ = std::make_unique<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter>(
        config_->limit(), encoder_callbacks_->encoderBufferLimit(),
        [this] { encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark(); },
        [this] { encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
        },
        [this] { encoder_callbacks_->continueEncoding(); }, config_->timeSource(),
        encoder_callbacks_->dispatcher(), encoder_callbacks_->scope(), config_->tokenBucket(),
        config_->fill_rate());
  }
  ENVOY_LOG(trace, "BandwidthLimiter: encode headers: egress_limiter_={}",
            egress_limiter_ ? true : false);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (egress_limiter_ != nullptr) {
    egress_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(trace, "BandwidthLimiter: encode data: egress_limiter_ not set");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthLimiter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (egress_limiter_ != nullptr) {
    return egress_limiter_->onTrailers() ? Http::FilterTrailersStatus::StopIteration
                                         : Http::FilterTrailersStatus::Continue;
  }
  return Http::FilterTrailersStatus::Continue;
}

const FilterConfig* BandwidthLimiter::getConfig() const {
  const auto* config = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      "envoy.filters.http.bandwidth_limit", decoder_callbacks_->route());
  if (config) {
    return config;
  }

  return config_.get();
}

void BandwidthLimiter::onDestroy() {
  if (ingress_limiter_ != nullptr) {
    ingress_limiter_->destroy();
  }
  if (egress_limiter_ != nullptr) {
    egress_limiter_->destroy();
  }
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
