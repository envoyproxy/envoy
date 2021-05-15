#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/http/utility.h"
#include "common/stats/timespan_impl.h"

using envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit;
using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

FilterConfig::FilterConfig(const BandwidthLimit& config, Stats::Scope& scope,
                           Runtime::Loader& runtime, TimeSource& time_source, bool per_route)
    : runtime_(runtime), time_source_(time_source),
      enable_mode_(config.enable_mode()),
      limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, limit_kbps, 0)),
      fill_interval_(std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
          config, fill_interval, StreamRateLimiter::DefaultFillInterval.count()))),
      enabled_(config.runtime_enabled(), runtime),
      stats_(generateStats(config.stat_prefix(), scope)) {
  if (per_route && !config.has_limit_kbps()) {
    throw EnvoyException("bandwidthlimitfilter: limit must be set for per route filter config");
  }

  // The token bucket is configured with a max token count of the number of
  // bytes per second, and refills at the same rate, so that we have a per
  // second limit which refills gradually in 1/fill_rate intervals.
  token_bucket_ = std::make_shared<SharedTokenBucketImpl>(
      StreamRateLimiter::kiloBytesToBytes(limit_kbps_), time_source,
      StreamRateLimiter::kiloBytesToBytes(limit_kbps_));
}

BandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_bandwidth_limit";
  return {ALL_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix),
                                    POOL_HISTOGRAM_PREFIX(scope, final_prefix))};
}

// BandwidthLimiter members

Http::FilterHeadersStatus BandwidthLimiter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto* config = getConfig();

  if (config->enabled() && (config->enableMode() & BandwidthLimit::Decode)) {
    config->stats().decode_enabled_.inc();
    decode_limiter_ = std::make_unique<StreamRateLimiter>(
        config->limit(), decoder_callbacks_->decoderBufferLimit(),
        [this] { decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark(); },
        [this] { decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          if (end_stream) {
            updateStatsOnDecodeFinish();
          }
          decoder_callbacks_->injectDecodedDataToFilterChain(data, end_stream);
        },
        [this] {
          updateStatsOnDecodeFinish();
          decoder_callbacks_->continueDecoding();
        },
        [config](uint64_t len) { config->stats().decode_allowed_size_.set(len); },
        const_cast<FilterConfig*>(config)->timeSource(), decoder_callbacks_->dispatcher(),
        decoder_callbacks_->scope(), config->tokenBucket(), config->fillInterval());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (decode_limiter_ != nullptr) {
    const auto* config = getConfig();

    if (!decode_latency_) {
      decode_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          config->stats().decode_transfer_duration_,
          const_cast<FilterConfig*>(config)->timeSource());
      config->stats().decode_pending_.inc();
    }
    config->stats().decode_incoming_size_.set(data.length());

    decode_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(debug, "BandwidthLimiter <decode data>: decode_limiter not set.");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthLimiter::decodeTrailers(Http::RequestTrailerMap&) {
  if (decode_limiter_ != nullptr) {
    if (decode_limiter_->onTrailers()) {
      return Http::FilterTrailersStatus::StopIteration;
    } else {
      updateStatsOnDecodeFinish();
      return Http::FilterTrailersStatus::Continue;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus BandwidthLimiter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  auto* config = getConfig();

  if (config->enabled() && (config->enableMode() & BandwidthLimit::Encode)) {
    config->stats().encode_enabled_.inc();

    encode_limiter_ = std::make_unique<StreamRateLimiter>(
        config->limit(), encoder_callbacks_->encoderBufferLimit(),
        [this] { encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark(); },
        [this] { encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          if (end_stream) {
            updateStatsOnEncodeFinish();
          }
          encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
        },
        [this] {
          updateStatsOnEncodeFinish();
          encoder_callbacks_->continueEncoding();
        },
        [config](uint64_t len) { config->stats().encode_allowed_size_.set(len); },
        const_cast<FilterConfig*>(config)->timeSource(), encoder_callbacks_->dispatcher(),
        encoder_callbacks_->scope(), config->tokenBucket(), config->fillInterval());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (encode_limiter_ != nullptr) {
    const auto* config = getConfig();

    if (!encode_latency_) {
      encode_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          config->stats().encode_transfer_duration_,
          const_cast<FilterConfig*>(config)->timeSource());
      config->stats().encode_pending_.inc();
    }
    config->stats().encode_incoming_size_.set(data.length());

    encode_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(debug, "BandwidthLimiter <encode data>: encode_limiter not set");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthLimiter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (encode_limiter_ != nullptr) {
    if (encode_limiter_->onTrailers()) {
      return Http::FilterTrailersStatus::StopIteration;
    } else {
      updateStatsOnEncodeFinish();
      return Http::FilterTrailersStatus::Continue;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

void BandwidthLimiter::updateStatsOnDecodeFinish() {
  if (decode_latency_) {
    decode_latency_->complete();
    decode_latency_.reset();
    getConfig()->stats().decode_pending_.dec();
  }
}

void BandwidthLimiter::updateStatsOnEncodeFinish() {
  if (encode_latency_) {
    encode_latency_->complete();
    encode_latency_.reset();
    getConfig()->stats().encode_pending_.dec();
  }
}

const FilterConfig* BandwidthLimiter::getConfig() const {
  const auto* config = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      "envoy.filters.http.mir_bandwidth_limit", decoder_callbacks_->route());
  if (config) {
    return config;
  }
  return config_.get();
}

void BandwidthLimiter::onDestroy() {
  if (decode_limiter_ != nullptr) {
    decode_limiter_->destroy();
  }
  if (encode_limiter_ != nullptr) {
    encode_limiter_->destroy();
  }
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
