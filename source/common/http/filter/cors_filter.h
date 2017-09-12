#pragma once

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Http {

class CorsFilter : public StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  CorsFilter();

  void initialize();

  // Http::StreamFilterBase
  void onDestroy() override {}

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
  };

private:
  friend class CorsFilterTest;

  const std::list<std::string>& allowOrigin();
  const std::string& allowMethods();
  const std::string& allowHeaders();
  const std::string& exposeHeaders();
  const std::string& maxAge();
  bool allowCredentials();
  bool enabled();
  bool isOriginAllowed(const Http::HeaderString& origin);

  StreamDecoderFilterCallbacks* decoder_callbacks_{};
  StreamEncoderFilterCallbacks* encoder_callbacks_{};
  const Envoy::Router::CorsPolicy* route_cors_policy_{};
  const Envoy::Router::CorsPolicy* virtual_host_cors_policy_{};
  bool is_cors_request_{};
  bool bad_route_{};
  const Http::HeaderEntry* origin_{};
};

} // namespace Http
} // namespace Envoy
