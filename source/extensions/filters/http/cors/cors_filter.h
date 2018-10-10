#pragma once

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

class CorsFilter : public Http::StreamFilter {
public:
  CorsFilter();

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };

private:
  friend class CorsFilterTest;

  const std::list<std::string>* allowOrigins();
  const std::list<std::regex>* allowOriginRegexes();
  const std::string& allowMethods();
  const std::string& allowHeaders();
  const std::string& exposeHeaders();
  const std::string& maxAge();
  bool allowCredentials();
  bool enabled();
  bool isOriginAllowed(const Http::HeaderString& origin);
  bool isOriginAllowedString(const Http::HeaderString& origin);
  bool isOriginAllowedRegex(const Http::HeaderString& origin);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  std::array<const Envoy::Router::CorsPolicy*, 2> policies_;
  bool is_cors_request_{};
  const Http::HeaderEntry* origin_{};
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
