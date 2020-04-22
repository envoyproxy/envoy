#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

class BufferFilterSettings : public Router::RouteSpecificFilterConfig {
public:
  BufferFilterSettings(const envoy::extensions::filters::http::buffer::v3::Buffer&);
  BufferFilterSettings(const envoy::extensions::filters::http::buffer::v3::BufferPerRoute&);

  bool disabled() const { return disabled_; }
  uint64_t maxRequestBytes() const { return max_request_bytes_; }

private:
  bool disabled_;
  uint64_t max_request_bytes_;
};

/**
 * Configuration for the buffer filter.
 */
class BufferFilterConfig {
public:
  BufferFilterConfig(const envoy::extensions::filters::http::buffer::v3::Buffer& proto_config);

  const BufferFilterSettings* settings() const { return &settings_; }

private:
  const BufferFilterSettings settings_;
};

using BufferFilterConfigSharedPtr = std::shared_ptr<BufferFilterConfig>;

/**
 * A filter that is capable of buffering an entire request before dispatching it upstream.
 */
class BufferFilter : public Http::StreamDecoderFilter {
public:
  BufferFilter(BufferFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void initConfig();
  void maybeAddContentLength();

  BufferFilterConfigSharedPtr config_;
  const BufferFilterSettings* settings_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RequestHeaderMap* request_headers_{};
  uint64_t content_length_{};
  bool config_initialized_{};
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
