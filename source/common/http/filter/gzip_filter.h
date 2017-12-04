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

using Compressor::ZlibCompressorImpl;

/**
 * Configuration for the gzip fiter.
 */
class GzipFilterConfig : Json::Validator {
public:
  GzipFilterConfig(const Json::Object& json_config)
      : Json::Validator(json_config, Json::Schema::GZIP_HTTP_FILTER_SCHEMA),
        compression_level_(parseLevel(json_config.getString("compression_level", "default"))),
        compression_strategy_(
            parseStrategy(json_config.getString("compression_strategy", "default"))),
        content_length_(json_config.getInteger("content_length", 32)),
        memory_level_(json_config.getInteger("memory_level", 8)),
        content_type_values_(json_config.getStringArray("content_type", true)),
        cache_control_values_(json_config.getStringArray("cache_control", true)),
        etag_(json_config.getBoolean("disable_on_etag", false)),
        last_modified_(json_config.getBoolean("disable_on_last_modified", false)) {}

  ZlibCompressorImpl::CompressionLevel getCompressionLevel() const { return compression_level_; }

  ZlibCompressorImpl::CompressionStrategy getCompressionStrategy() const {
    return compression_strategy_;
  }

  std::vector<std::string> getCacheControlValues() const { return cache_control_values_; }

  std::vector<std::string> getContentTypeValues() const { return content_type_values_; }

  uint64_t getMinimumLength() const { return content_length_; }

  uint64_t getMemoryLevel() const { return memory_level_; }

  bool disableOnEtag() const { return etag_; }

  bool disableOnLastModified() const { return last_modified_; }

private:
  static ZlibCompressorImpl::CompressionLevel parseLevel(const std::string level) {
    if (level == "best") {
      return ZlibCompressorImpl::CompressionLevel::Best;
    }
    if (level == "speed") {
      return ZlibCompressorImpl::CompressionLevel::Speed;
    }
    return ZlibCompressorImpl::CompressionLevel::Standard;
  }

  static ZlibCompressorImpl::CompressionStrategy parseStrategy(const std::string strategy) {
    if (strategy == "rle") {
      return Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
    }
    if (strategy == "filtered") {
      return ZlibCompressorImpl::CompressionStrategy::Filtered;
    }
    if (strategy == "huffman") {
      return Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
    }
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
  }

  const ZlibCompressorImpl::CompressionLevel compression_level_;
  const ZlibCompressorImpl::CompressionStrategy compression_strategy_;
  const uint64_t content_length_;
  const uint64_t memory_level_;
  const std::vector<std::string> content_type_values_;
  const std::vector<std::string> cache_control_values_;
  const bool etag_;
  const bool last_modified_;
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
  bool isContentTypeAllowed(const HeaderMap& headers) const;
  bool isMinimumContentLength(const HeaderMap& headers) const;
  bool isCacheControlAllowed(const HeaderMap& headers) const;

  bool skip_compression_;
  Buffer::OwnedImpl compressed_data_;
  Compressor::ZlibCompressorImpl compressor_;

  const std::string allowed_types_pattern_{
      ".*(text/html|text/css|text/plain|text/xml|"
      "application/javascript|application/json|application/xml|"
      "font/eot|font/opentype|font/otf|image/svg+xml).*"};

  GzipFilterConfigSharedPtr config_{nullptr};

  const Http::HeaderEntry* accept_encoding_{nullptr};

  StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Http
} // namespace Envoy
