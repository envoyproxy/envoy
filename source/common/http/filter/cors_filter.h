#pragma once

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Http {

/**
 * Configuration for the cors filter.
 */
struct CorsFilterConfig {};

typedef std::shared_ptr<const CorsFilterConfig> CorsFilterConfigConstSharedPtr;

class CorsFilter : public StreamFilter {
public:
  CorsFilter(CorsFilterConfigConstSharedPtr config);
  ~CorsFilter();

  void initialize();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return FilterDataStatus::Continue;
  };
  FilterTrailersStatus decodeTrailers(HeaderMap&) override {
    return FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return FilterTrailersStatus::Continue;
  };
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  CorsFilterConfigConstSharedPtr config_;
  StreamDecoderFilterCallbacks* decoder_callbacks_{};
  StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool cors_enabled_{};
  bool is_cors_request_{};
  std::string allow_origin_{};
  std::string allow_methods_{};
  std::string allow_headers_{};
  std::string expose_headers_{};
  std::string max_age_{};
  bool allow_credentials_{};
  Http::HeaderEntry* origin_{};
};

} // namespace Http
} // namespace Envoy
