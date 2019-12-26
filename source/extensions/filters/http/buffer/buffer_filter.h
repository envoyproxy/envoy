#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

class BufferFilterSettings : public Router::RouteSpecificFilterConfig {
public:
  BufferFilterSettings(const envoy::config::filter::http::buffer::v2::Buffer&);
  BufferFilterSettings(const envoy::config::filter::http::buffer::v2::BufferPerRoute&);

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
  BufferFilterConfig(const envoy::config::filter::http::buffer::v2::Buffer& proto_config);

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
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void initConfig();
  void maybeAddContentLength();

  BufferFilterConfigSharedPtr config_;
  const BufferFilterSettings* settings_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::HeaderMap* request_headers_{};
  uint64_t content_length_{};
  bool config_initialized_{};
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
