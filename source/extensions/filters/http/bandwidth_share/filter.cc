#include "source/extensions/filters/http/bandwidth_share/filter.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/stats/timespan_impl.h"

#include "stats.h"

using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

const std::string& BandwidthShare::filterName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.http.bandwidth_share");
}

void BandwidthShare::StatsInProgress::update(uint64_t length_sent, uint64_t bytes_buffered,
                                             std::chrono::milliseconds delay) {
  ASSERT(stats_.has_value());
  if (bytes_buffered) {
    stats_->bytes_limited_.add(length_sent);
    if (bytes_buffered_ > bytes_buffered) {
      stats_->bytes_pending_.sub(bytes_buffered_ - bytes_buffered);
    } else {
      stats_->bytes_pending_.add(bytes_buffered - bytes_buffered_);
      if (bytes_buffered_ == 0) {
        stats_->streams_currently_limited_.inc();
      }
    }
    bytes_buffered_ = bytes_buffered;
    delay_ += delay;
  } else {
    stats_->bytes_not_limited_.add(length_sent);
    if (bytes_buffered_ > 0) {
      stats_->bytes_pending_.sub(bytes_buffered_);
      stats_->streams_currently_limited_.dec();
      bytes_buffered_ = 0;
    }
  }
}

Http::FilterHeadersStatus BandwidthShare::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                        bool) {
  const auto& config = getConfig();
  request_state_.start_time_ = config.timeSource().monotonicTime();
  if (config.enabled()) {
    tenant_ = config.getTenantName(decoder_callbacks_->streamInfo(), request_headers);
    request_bucket_ = config.getRequestBucket(tenant_);
  }
  if (request_bucket_) {
    request_state_.stats_ = config.requestStatsForTenant(tenant_);
    request_limiter_ = std::make_unique<StreamRateLimiter>(
        decoder_callbacks_->bufferLimit(),
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
        [this](uint64_t len, uint64_t buffered, std::chrono::milliseconds delay) {
          request_state_.update(len, buffered, delay);
        },
        decoder_callbacks_->dispatcher(), decoder_callbacks_->scope(), request_bucket_,
        request_bucket_->fillInterval());
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthShare::decodeData(Buffer::Instance& data, bool end_stream) {
  if (request_limiter_ != nullptr) {
    request_limiter_->writeData(data, end_stream);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BandwidthShare::decodeTrailers(Http::RequestTrailerMap&) {
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

Http::FilterHeadersStatus BandwidthShare::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  auto& config = getConfig();
  response_state_.start_time_ = config.timeSource().monotonicTime();
  response_bucket_ = config.getResponseBucket(tenant_);
  if (response_bucket_) {
    response_state_.stats_ = config.responseStatsForTenant(tenant_);
    response_limiter_ = std::make_unique<StreamRateLimiter>(
        encoder_callbacks_->bufferLimit(),
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
        [this](uint64_t len, uint64_t buffered_bytes, std::chrono::milliseconds delay) {
          response_state_.update(len, buffered_bytes, delay);
        },
        encoder_callbacks_->dispatcher(), encoder_callbacks_->scope(), response_bucket_,
        response_bucket_->fillInterval());
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BandwidthShare::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_limiter_ != nullptr) {
    const auto& config = getConfig();

    // Adds encoded trailers. May only be called in encodeData when end_stream is set to true.
    // If upstream has trailers, addEncodedTrailers won't be called
    bool trailer_added = false;
    if (end_stream && config.enableResponseTrailers()) {
      encoder_callbacks_->addEncodedTrailers();
      trailer_added = true;
    }

    response_limiter_->writeData(data, end_stream, trailer_added);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
BandwidthShare::encodeTrailers(Http::ResponseTrailerMap& response_trailers) {
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

void BandwidthShare::updateStatsOnDecodeFinish() {
  const auto& config = getConfig();
  request_duration_ = std::chrono::duration_cast<std::chrono::milliseconds>(
      config.timeSource().monotonicTime() - request_state_.start_time_);
}

void BandwidthShare::updateStatsOnEncodeFinish() {
  const auto& config = getConfig();
  std::chrono::milliseconds response_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(config.timeSource().monotonicTime() -
                                                            response_state_.start_time_);

  if (response_bucket_) {
    if (config.enableResponseTrailers() && trailers_ != nullptr) {
      if (request_duration_ > std::chrono::milliseconds{0}) {
        trailers_->setCopy(config.responseTrailers().request_duration_,
                           std::to_string(request_duration_.count()));
      }
      if (request_state_.delay_ > std::chrono::milliseconds{0}) {
        trailers_->setCopy(config.responseTrailers().request_delay_,
                           std::to_string(request_state_.delay_.count()));
      }
      if (response_duration > std::chrono::milliseconds{0}) {
        trailers_->setCopy(config.responseTrailers().response_duration_,
                           std::to_string(response_duration.count()));
      }
      if (response_state_.delay_ > std::chrono::milliseconds{0}) {
        trailers_->setCopy(config.responseTrailers().response_delay_,
                           std::to_string(response_state_.delay_.count()));
      }
    }
  }
}

const FilterConfig& BandwidthShare::getConfig() {
  if (selected_shared_state_) {
    return *selected_shared_state_;
  }
  auto* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(decoder_callbacks_);
  if (config) {
    selected_shared_state_.emplace(*config);
  } else {
    selected_shared_state_.emplace(*shared_state_);
  }
  return *selected_shared_state_;
}

void BandwidthShare::onDestroy() {
  if (request_limiter_ != nullptr) {
    request_limiter_->destroy();
  }
  if (response_limiter_ != nullptr) {
    response_limiter_->destroy();
  }
  if (request_state_.bytes_buffered_ > 0) {
    request_state_.stats_->bytes_pending_.sub(request_state_.bytes_buffered_);
    request_state_.stats_->streams_currently_limited_.dec();
    request_state_.bytes_buffered_ = 0;
  }
  if (response_state_.bytes_buffered_ > 0) {
    response_state_.stats_->bytes_pending_.sub(response_state_.bytes_buffered_);
    response_state_.stats_->streams_currently_limited_.dec();
    response_state_.bytes_buffered_ = 0;
  }
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
