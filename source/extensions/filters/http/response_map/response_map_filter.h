#pragma once

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/response_map/response_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

/**
 * Configuration for the HTTP response filter.
 */
class ResponseMapFilterConfig {
public:
  ResponseMapFilterConfig(
      const envoy::extensions::filters::http::response_map::v3::ResponseMap& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context);
  const ResponseMap::ResponseMapPtr& response_map() { return response_map_; }

private:
  const ResponseMap::ResponseMapPtr response_map_;
};
using ResponseMapFilterConfigSharedPtr = std::shared_ptr<ResponseMapFilterConfig>;

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::extensions::filters::http::response_map::v3::ResponseMapPerRoute&
          config)
      : disabled_(config.disabled()) {}
  bool disabled() const { return disabled_; }

private:
  bool disabled_;
};

class ResponseMapFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  ResponseMapFilter(ResponseMapFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&,
                                          bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };

  void doRewrite();

private:
  ResponseMapFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::ResponseHeaderMap* response_headers_{};
  Http::RequestHeaderMap* request_headers_{};
  bool do_rewrite_{};
  bool disabled_{};
};

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
