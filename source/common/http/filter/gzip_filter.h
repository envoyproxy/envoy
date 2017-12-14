#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"

#include "common/buffer/buffer_impl.h"
#include "common/compressor/zlib_compressor_impl.h"
#include "common/http/header_map_impl.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"

#include "api/filter/http/gzip.pb.h"

namespace Envoy {
namespace Http {

using Compressor::ZlibCompressorImpl;

/**
 * Configuration for the gzip fiter.
 */
class GzipFilterConfig {
public:
  GzipFilterConfig(const envoy::api::v2::filter::http::Gzip& gzip)
      : compression_level_(compressionLevelEnum(gzip.compression_level())),
        compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
        content_length_(static_cast<uint64_t>(gzip.content_length().value())),
        memory_level_(static_cast<uint64_t>(gzip.memory_level().value())),
        etag_(gzip.disable_on_etag().value()),
        last_modified_(gzip.disable_on_last_modified().value()) {
    for (const auto& value : gzip.cache_control()) {
      cache_control_values_.insert(value);
    }
    for (const auto& value : gzip.content_type()) {
      content_type_values_.insert(value);
    }
  }

  ZlibCompressorImpl::CompressionLevel compressionLevel() const { return compression_level_; }

  ZlibCompressorImpl::CompressionStrategy compressionStrategy() const {
    return compression_strategy_;
  }

  const std::unordered_set<std::string>& cacheControlValues() const {
    return cache_control_values_;
  }

  const std::unordered_set<std::string>& contentTypeValues() const { return content_type_values_; }

  uint64_t minimumLength() const { return content_length_ > 29 ? content_length_ : 30; }

  uint64_t memoryLevel() const { return memory_level_ > 0 ? memory_level_ : 8; }

  bool disableOnEtag() const { return etag_; }

  bool disableOnLastModified() const { return last_modified_; }

private:
  static ZlibCompressorImpl::CompressionLevel compressionLevelEnum(const auto& compression_level) {
    if (compression_level == envoy::api::v2::filter::http::Gzip_CompressionLevel_Enum_BEST) {
      return ZlibCompressorImpl::CompressionLevel::Best;
    }
    if (compression_level == envoy::api::v2::filter::http::Gzip_CompressionLevel_Enum_SPEED) {
      return ZlibCompressorImpl::CompressionLevel::Speed;
    }
    return ZlibCompressorImpl::CompressionLevel::Standard;
  }

  static ZlibCompressorImpl::CompressionStrategy
  compressionStrategyEnum(const auto& compression_strategy) {
    if (compression_strategy == envoy::api::v2::filter::http::Gzip_CompressionStrategy_RLE) {
      return Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
    }
    if (compression_strategy == envoy::api::v2::filter::http::Gzip_CompressionStrategy_FILTERED) {
      return ZlibCompressorImpl::CompressionStrategy::Filtered;
    }
    if (compression_strategy == envoy::api::v2::filter::http::Gzip_CompressionStrategy_HUFFMAN) {
      return Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
    }
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
  }

  ZlibCompressorImpl::CompressionLevel compression_level_;
  ZlibCompressorImpl::CompressionStrategy compression_strategy_;
  int32_t content_length_;
  int32_t memory_level_;
  std::unordered_set<std::string> content_type_values_;
  std::unordered_set<std::string> cache_control_values_;
  bool etag_;
  bool last_modified_;
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
  bool isAcceptEncodingGzip(const HeaderMap& headers) const;
  bool isCacheControlAllowed(const HeaderMap& headers) const;
  bool isContentTypeAllowed(const HeaderMap& headers) const;
  bool isMinimumContentLength(const HeaderMap& headers) const;
  bool isEtagAllowed(const HeaderMap& headers) const;
  bool isLastModifiedAllowed(const HeaderMap& headers) const;
  bool isTransferEncodingAllowed(const HeaderMap& headers) const;

  bool skip_compression_;

  Buffer::OwnedImpl compressed_data_;

  Compressor::ZlibCompressorImpl compressor_;

  GzipFilterConfigSharedPtr config_{nullptr};

  StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Http
} // namespace Envoy
