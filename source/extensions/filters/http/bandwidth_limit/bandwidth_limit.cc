#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/stats/timespan_impl.h"

using envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit;
using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

namespace {
const Http::LowerCaseString DefaultRequestDelayTrailer =
    Http::LowerCaseString("bandwidth-request-delay-ms");
const Http::LowerCaseString DefaultResponseDelayTrailer =
    Http::LowerCaseString("bandwidth-response-delay-ms");
const std::chrono::milliseconds ZeroMilliseconds = std::chrono::milliseconds(0);
} // namespace

FilterConfig::FilterConfig(const BandwidthLimit& config, Stats::Scope& scope,
                           Runtime::Loader& runtime, TimeSource& time_source, bool per_route)
    : runtime_(runtime), time_source_(time_source), enable_mode_(config.enable_mode()),
      limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, limit_kbps, 0)),
      fill_interval_(std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
          config, fill_interval, StreamRateLimiter::DefaultFillInterval.count()))),
      enabled_(config.runtime_enabled(), runtime),
      stats_(generateStats(config.stat_prefix(), scope)),
      request_delay_trailer_(
          config.response_trailer_prefix().empty()
              ? DefaultRequestDelayTrailer
              : Http::LowerCaseString(absl::StrCat(config.response_trailer_prefix(), "-",
                                                   DefaultRequestDelayTrailer.get()))),
      response_delay_trailer_(
          config.response_trailer_prefix().empty()
              ? DefaultResponseDelayTrailer
              : Http::LowerCaseString(absl::StrCat(config.response_trailer_prefix(), "-",
                                                   DefaultResponseDelayTrailer.get()))),
      enable_response_trailers_(config.enable_response_trailers()) {
  if (per_route && !config.has_limit_kbps()) {
    throw EnvoyException("bandwidthlimitfilter: limit must be set for per route filter config");
  }

  // The token bucket is configured with a max token count of the number of
  // bytes per second, and refills at the same rate, so that we have a per
  // second limit which refills gradually in 1/fill_interval increments.
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
  const auto& config = getConfig();

  if (config.enabled() && (config.enableMode() & BandwidthLimit::REQUEST)) {
    config.stats().request_enabled_.inc();
    request_limiter_ = std::make_unique<StreamRateLimiter>(
        config.limit(), decoder_callbacks_->decoderBufferLimit(),
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
        [&config](uint64_t len, bool limit_enforced) {
          config.stats().request_allowed_size_.set(len);
          if (limit_enforced) {
            config.stats().request_enforced_.inc();
          }
        },
        const_cast<FilterConfig*>(&config)->timeSource(), decoder_callbacks_->dispatcher(),
        decoder_callbacks_->scope(), config.tokenBucket(), config.fillInterval());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (request_limiter_ != nullptr) {
    const auto& config = getConfig();

    if (!request_latency_) {
      request_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          config.stats().request_transfer_duration_,
          const_cast<FilterConfig*>(&config)->timeSource());
      config.stats().request_pending_.inc();
    }
    config.stats().request_incoming_size_.set(data.length());

    request_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(debug, "BandwidthLimiter <decode data>: request_limiter not set.");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthLimiter::decodeTrailers(Http::RequestTrailerMap&) {
  if (request_limiter_ != nullptr) {
    if (request_limiter_->onTrailers()) {
      return Http::FilterTrailersStatus::StopIteration;
    } else {
      updateStatsOnDecodeFinish();
      return Http::FilterTrailersStatus::Continue;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus BandwidthLimiter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  auto& config = getConfig();

  if (config.enabled() && (config.enableMode() & BandwidthLimit::RESPONSE)) {
    config.stats().response_enabled_.inc();

    response_limiter_ = std::make_unique<StreamRateLimiter>(
        config.limit(), encoder_callbacks_->encoderBufferLimit(),
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
        [&config](uint64_t len, bool limit_enforced) {
          config.stats().response_allowed_size_.set(len);
          if (limit_enforced) {
            config.stats().response_enforced_.inc();
          }
        },
        const_cast<FilterConfig*>(&config)->timeSource(), encoder_callbacks_->dispatcher(),
        encoder_callbacks_->scope(), config.tokenBucket(), config.fillInterval());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_limiter_ != nullptr) {
    const auto& config = getConfig();

    // Adds encoded trailers. May only be called in encodeData when end_stream is set to true.
    // If upstream has trailers, addEncodedTrailers won't be called
    bool trailer_added = false;
    if (end_stream && config.enableResponseTrailers()) {
      trailers_ = &encoder_callbacks_->addEncodedTrailers();
      trailer_added = true;
    }

    if (!response_latency_) {
      response_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          config.stats().response_transfer_duration_,
          const_cast<FilterConfig*>(&config)->timeSource());
      config.stats().response_pending_.inc();
    }
    config.stats().response_incoming_size_.set(data.length());

    response_limiter_->writeData(data, end_stream, trailer_added);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  ENVOY_LOG(debug, "BandwidthLimiter <encode data>: response_limiter not set");
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
BandwidthLimiter::encodeTrailers(Http::ResponseTrailerMap& response_trailers) {
  if (response_limiter_ != nullptr) {
    trailers_ = &response_trailers;

    if (response_limiter_->onTrailers()) {
      return Http::FilterTrailersStatus::StopIteration;
    } else {
      updateStatsOnEncodeFinish();
      return Http::FilterTrailersStatus::Continue;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

void BandwidthLimiter::updateStatsOnDecodeFinish() {
  if (request_latency_) {
    request_duration_ = request_latency_.get()->elapsed();
    request_latency_->complete();
    request_latency_.reset();
    getConfig().stats().request_pending_.dec();
  }
}

void BandwidthLimiter::updateStatsOnEncodeFinish() {
  if (response_latency_) {
    const auto& config = getConfig();

    if (config.enableResponseTrailers() && trailers_ != nullptr) {
      auto response_duration = response_latency_.get()->elapsed();
      if (request_duration_ > ZeroMilliseconds) {
        trailers_->setCopy(config.requestDelayTrailer(), std::to_string(request_duration_.count()));
      }
      if (response_duration > ZeroMilliseconds) {
        trailers_->setCopy(config.responseDelayTrailer(),
                           std::to_string(response_duration.count()));
      }
    }

    response_latency_->complete();
    response_latency_.reset();
    config.stats().response_pending_.dec();
  }
}

const FilterConfig& BandwidthLimiter::getConfig() const {
  const auto* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(decoder_callbacks_);
  if (config) {
    return *config;
  }
  return *config_;
}

void BandwidthLimiter::onDestroy() {
  if (request_limiter_ != nullptr) {
    request_limiter_->destroy();
  }
  if (response_limiter_ != nullptr) {
    response_limiter_->destroy();
  }
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
