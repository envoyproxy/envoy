#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"

#include "common/buffer/buffer_impl.h"
#include "common/compressor/zlib_compressor_impl.h"
#include "common/http/header_map_impl.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Http {

/**
 * Configuration for the gzip fiter.
 */
class GzipFilterConfig : Json::Validator {
public:
  GzipFilterConfig(const Json::Object& json_config)
      : Json::Validator(json_config, Json::Schema::GZIP_HTTP_FILTER_SCHEMA),
        compression_level_(json_config.getString("compression_level", "default")),
        content_types_(json_config.getStringArray("content_types", true)),
        memory_level_(json_config.getInteger("memory_level", 8)) {}

  uint64_t getMemoryLevel() const { return memory_level_; }

  Compressor::ZlibCompressorImpl::CompressionLevel getCompressionLevel() const {
    if (compression_level_ == "best") {
      return Compressor::ZlibCompressorImpl::CompressionLevel::Best;
    }
    if (compression_level_ == "speed") {
      return Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
    }

    return Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }

  std::vector<std::string> getContentTypes() const { return content_types_; }

private:
  const std::string compression_level_{};
  const std::vector<std::string> content_types_{};
  const uint64_t memory_level_{};
};

typedef std::shared_ptr<GzipFilterConfig> GzipFilterConfigSharedPtr;

/**
 * A filter that compresses data dispatched from the upstream upon client request.
 */
class GzipFilter : public Http::StreamFilter {
public:
  GzipFilter(GzipFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return FilterDataStatus::Continue;
  }
  FilterTrailersStatus decodeTrailers(HeaderMap&) override {
    return FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // Http::StreamEncoderFilter
  FilterHeadersStatus encodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  FilterTrailersStatus encodeTrailers(HeaderMap&) override {
    return FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  bool isContentTypeAllowed(const HeaderMap& headers);

  Buffer::OwnedImpl compressed_data_{};
  Compressor::ZlibCompressorImpl compressor_{};

  GzipFilterConfigSharedPtr config_{nullptr};

  Http::HeaderMap* request_headers_{nullptr};
  Http::HeaderMap* response_headers_{nullptr};

  StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Http
} // namespace Envoy
