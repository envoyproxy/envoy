#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include <string>

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
const Http::LowerCaseString DefaultRequestFilterDelayTrailer =
    Http::LowerCaseString("bandwidth-request-filter-delay-ms");
const Http::LowerCaseString DefaultResponseFilterDelayTrailer =
    Http::LowerCaseString("bandwidth-response-filter-delay-ms");
} // namespace

absl::StatusOr<std::shared_ptr<FilterConfig>> FilterConfig::create(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& config,
    std::shared_ptr<NamedBucketSelector> named_bucket_selector, Stats::Scope& scope,
    Runtime::Loader& runtime, TimeSource& time_source, bool per_route) {
  auto status = absl::OkStatus();
  auto filter_config = std::shared_ptr<FilterConfig>(new FilterConfig(
      config, std::move(named_bucket_selector), scope, runtime, time_source, per_route, status));
  RETURN_IF_NOT_OK_REF(status);
  return filter_config;
}

FilterConfig::FilterConfig(const BandwidthLimit& config,
                           std::shared_ptr<NamedBucketSelector> named_bucket_selector,
                           Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source,
                           bool per_route, absl::Status& creation_status)
    : named_bucket_selector_(named_bucket_selector), time_source_(time_source),
      enable_mode_(config.enable_mode()),
      limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, limit_kbps, 0)),
      enabled_(config.runtime_enabled(), runtime),
      bucket_and_stats_(
          config.stat_prefix(), time_source, scope, limit_kbps_,
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
              config, fill_interval, StreamRateLimiter::DefaultFillInterval.count()))),
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
      request_filter_delay_trailer_(
          config.response_trailer_prefix().empty()
              ? DefaultRequestFilterDelayTrailer
              : Http::LowerCaseString(absl::StrCat(config.response_trailer_prefix(), "-",
                                                   DefaultRequestFilterDelayTrailer.get()))),
      response_filter_delay_trailer_(
          config.response_trailer_prefix().empty()
              ? DefaultResponseFilterDelayTrailer
              : Http::LowerCaseString(absl::StrCat(config.response_trailer_prefix(), "-",
                                                   DefaultResponseFilterDelayTrailer.get()))),
      enable_response_trailers_(config.enable_response_trailers()) {
  creation_status = absl::OkStatus();

  if (per_route && !config.has_limit_kbps()) {
    creation_status = absl::InvalidArgumentError("limit must be set for per route filter config");
    return;
  }
}

OptRef<const BucketAndStats>
FilterConfig::bucketAndStats(const StreamInfo::StreamInfo& stream_info) const {
  if (!named_bucket_selector_) {
    return bucket_and_stats_;
  }
  return named_bucket_selector_->getBucket(stream_info);
}

std::shared_ptr<SharedTokenBucketImpl> BandwidthLimiter::bucket() const {
  return bucket_and_stats_.has_value() ? bucket_and_stats_->bucket() : nullptr;
}
OptRef<BandwidthLimitStats> BandwidthLimiter::stats() const {
  ASSERT(bucket_and_stats_.has_value());
  return bucket_and_stats_->stats();
}
std::chrono::milliseconds BandwidthLimiter::fillInterval() const {
  ASSERT(bucket_and_stats_.has_value());
  return bucket_and_stats_->fillInterval();
}

Http::FilterHeadersStatus BandwidthLimiter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto& config = getConfig();
  bucket_and_stats_ = config.bucketAndStats(decoder_callbacks_->streamInfo());

  if (bucket() && config.enabled() && (config.enableMode() & BandwidthLimit::REQUEST)) {
    stats()->request_enabled_.inc();
    request_limiter_ = std::make_unique<StreamRateLimiter>(
        config.limit(), decoder_callbacks_->bufferLimit(),
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
        [this](uint64_t len, bool limit_enforced, std::chrono::milliseconds delay) {
          stats()->request_allowed_size_.set(len);
          stats()->request_allowed_total_size_.add(len);
          if (limit_enforced) {
            stats()->request_enforced_.inc();
            request_delay_ += delay;
          }
        },
        const_cast<FilterConfig*>(&config)->timeSource(), decoder_callbacks_->dispatcher(),
        decoder_callbacks_->scope(), bucket(), fillInterval());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthLimiter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (request_limiter_ != nullptr) {
    const auto& config = getConfig();

    if (!request_latency_) {
      request_latency_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          stats()->request_transfer_duration_, const_cast<FilterConfig*>(&config)->timeSource());
      stats()->request_pending_.inc();
    }
    stats()->request_incoming_size_.set(data.length());
    stats()->request_incoming_total_size_.add(data.length());

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
  if (!bucket_and_stats_.has_value()) {
    // This shouldn't be necessary since decodeHeaders should always be called before encodeHeaders,
    // but it's expedient here because tests don't always do that.
    bucket_and_stats_ = config.bucketAndStats(decoder_callbacks_->streamInfo());
  }

  if (bucket() && config.enabled() && (config.enableMode() & BandwidthLimit::RESPONSE)) {
    stats()->response_enabled_.inc();

    response_limiter_ = std::make_unique<StreamRateLimiter>(
        config.limit(), encoder_callbacks_->bufferLimit(),
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
        [this](uint64_t len, bool limit_enforced, std::chrono::milliseconds delay) {
          stats()->response_allowed_size_.set(len);
          stats()->response_allowed_total_size_.add(len);
          if (limit_enforced) {
            stats()->response_enforced_.inc();
            response_delay_ += delay;
          }
        },
        const_cast<FilterConfig*>(&config)->timeSource(), encoder_callbacks_->dispatcher(),
        encoder_callbacks_->scope(), bucket(), fillInterval());
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
          stats()->response_transfer_duration_, const_cast<FilterConfig*>(&config)->timeSource());
      stats()->response_pending_.inc();
    }
    stats()->response_incoming_size_.set(data.length());
    stats()->response_incoming_total_size_.add(data.length());

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
    if (stats()) {
      stats()->request_pending_.dec();
    }
  }
}

void BandwidthLimiter::updateStatsOnEncodeFinish() {
  if (response_latency_) {
    const auto& config = getConfig();

    if (config.enableResponseTrailers() && trailers_ != nullptr) {
      auto response_duration = response_latency_.get()->elapsed();
      if (request_duration_ > zero_milliseconds_) {
        trailers_->setCopy(config.requestDelayTrailer(), std::to_string(request_duration_.count()));
      }
      if (request_delay_ > zero_milliseconds_) {
        trailers_->setCopy(config.requestFilterDelayTrailer(),
                           std::to_string(request_delay_.count()));
        request_delay_ = zero_milliseconds_;
      }
      if (response_duration > zero_milliseconds_) {
        trailers_->setCopy(config.responseDelayTrailer(),
                           std::to_string(response_duration.count()));
      }
      if (response_delay_ > zero_milliseconds_) {
        trailers_->setCopy(config.responseFilterDelayTrailer(),
                           std::to_string(response_delay_.count()));
        response_delay_ = zero_milliseconds_;
      }
    }

    response_latency_->complete();
    response_latency_.reset();
    stats()->response_pending_.dec();
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
