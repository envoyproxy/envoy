#pragma once

#include <memory>

#include "envoy/common/optref.h"
#include "envoy/common/time.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/bandwidth_share/filter_config.h"
#include "source/extensions/filters/http/common/stream_rate_limiter.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

class BandwidthShare : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  BandwidthShare(std::shared_ptr<FilterConfig> shared_state)
      : shared_state_(std::move(shared_state)) {}

  static const std::string& filterName();

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override;

private:
  struct StatsInProgress {
    MonotonicTime start_time_;
    uint64_t bytes_buffered_{0};
    std::chrono::milliseconds delay_{0};
    OptRef<BandwidthShareStats> stats_;
    void update(uint64_t length_sent, uint64_t bytes_buffered, std::chrono::milliseconds delay);
  };
  friend class FilterTest;
  const FilterConfig& getConfig();

  void updateStatsOnDecodeFinish();
  void updateStatsOnEncodeFinish();

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  const std::shared_ptr<const FilterConfig> shared_state_;
  OptRef<const FilterConfig> selected_shared_state_;
  std::shared_ptr<FairTokenBucket::Client> request_bucket_;
  std::shared_ptr<FairTokenBucket::Client> response_bucket_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> request_limiter_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> response_limiter_;
  std::string tenant_;
  StatsInProgress request_state_, response_state_;
  std::chrono::milliseconds request_duration_{0};
  Http::ResponseTrailerMap* trailers_{nullptr};
};

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
