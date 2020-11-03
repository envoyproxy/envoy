#pragma once

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/response_map/response_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

/**
 * Configuration for the response map filter.
 */
class ResponseMapFilterConfig {
public:
  ResponseMapFilterConfig(
      const envoy::extensions::filters::http::response_map::v3::ResponseMap& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context);
  const ResponseMap::ResponseMapPtr* response_map() const { return &response_map_; }

private:
  const ResponseMap::ResponseMapPtr response_map_;
};
using ResponseMapFilterConfigSharedPtr = std::shared_ptr<ResponseMapFilterConfig>;

/*
 * Per-route configuration for the response map filter.
 * Allows the response map to be overriden or disabled.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::extensions::filters::http::response_map::v3::ResponseMapPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validationVisitor);
  bool disabled() const { return disabled_; }
  const ResponseMap::ResponseMapPtr* response_map() const { return &response_map_; }

private:
  bool disabled_;
  const ResponseMap::ResponseMapPtr response_map_;
};

/**
 * The response map filter.
 *
 * Filters on both the decoding stage (request moving upstream) and the encoding
 * stage (response moving downstream).
 *
 * The request headers are captured in decodeHeaders to be later passed to a response
 * map mapper for matching purposes.
 *
 * The response headers are captured in encodeHeaders to both be inspected by a
 * response mapper for matching purposes. At this point, we decide if the response
 * body (or, in theory, the headers too) will be rewritten. If the response is
 * headers-only, we do the rewrite right away because encodeData won't be called.
 *
 * Otherwise, we wait until encodeData, drain anything we get from the upstream,
 * and finally rewrite the response body.
 *
 * Not all filter stages are guaranteed to be called. For example, if there are
 * no request headers to parse (because, for example, Envoy responds locally
 * with HTTP 426 to upgrade an HTTP/1.0 request before parsing headers), then
 * decodeHeaders will never be called. Similarly, if there is no upstream
 * response body, then encodeData will not be called.
 *
 * The response map filter maintains three pieces of state:
 *
 *   disabled_: set to true if a per-route config is found in the decode stage and
 *              the disabled flag is set. this disables rewrite behavior entirely.
 *
 *   do_rewrite_: set to true if the chosen response mapper matched, and we should
 *                eventually do a response body (and/or header) rewrite.
 *
 *   response_map_: set to a pointer to the response map that should be used to match
 *                  and rewrite. if a per-route config is found and its mapper is set,
 *                  use that. otherwise, use the globally configured mapper.
 */
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

  // Get the route specific config to use for this filter iteration.
  const FilterConfigPerRoute* getRouteSpecificConfig(void);

  // Do the actual rewrite using the mapper we decided to use.
  //
  // Cannot be called if the filter was disabled (disabled_ == true) or if we decided
  // not to do the rewrite (do_rewrite_ == false). Requires that response_map_ is set.
  void doRewrite();

private:
  ResponseMapFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::ResponseHeaderMap* response_headers_{};
  Http::RequestHeaderMap* request_headers_{};

  // True if the response map is disabled on this iteration of the filter chain.
  bool disabled_{};

  // True if the response map matched the response and so we should rewrite
  // the response. False otherwise.
  bool do_rewrite_{};

  // The response_map to use. May be the global response_map or a per-route map.
  const ResponseMap::ResponseMapPtr* response_map_{};
};

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
