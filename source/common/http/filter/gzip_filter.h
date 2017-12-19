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

using ZlibCompressionLevelEnum = Compressor::ZlibCompressorImpl::CompressionLevel;
using ZlibCompressionStrategyEnum = Compressor::ZlibCompressorImpl::CompressionStrategy;

using GzipV2CompressionLevelEnum = envoy::api::v2::filter::http::Gzip_CompressionLevel_Enum;
using GzipV2CompressionStrategyEnum = envoy::api::v2::filter::http::Gzip_CompressionStrategy;

/**
 * Configuration for the gzip filter.
 */
class GzipFilterConfig {
public:
  GzipFilterConfig(const envoy::api::v2::filter::http::Gzip& gzip);

  ZlibCompressionLevelEnum compressionLevel() const { return compression_level_; }
  ZlibCompressionStrategyEnum compressionStrategy() const { return compression_strategy_; }
  const std::unordered_set<std::string>& contentTypeValues() const { return content_type_values_; }
  const std::unordered_set<std::string>& cacheControlValues() const {
    return cache_control_values_;
  }
  uint64_t minimumLength() const { return content_length_ > 29 ? content_length_ : 30; }
  uint64_t memoryLevel() const { return memory_level_ > 0 ? memory_level_ : 8; }
  bool disableOnEtag() const { return etag_; }
  bool disableOnLastModified() const { return last_modified_; }

private:
  static ZlibCompressionLevelEnum
  compressionLevelEnum(const GzipV2CompressionLevelEnum& compression_level);
  static ZlibCompressionStrategyEnum
  compressionStrategyEnum(const GzipV2CompressionStrategyEnum& compression_strategy);

  ZlibCompressionLevelEnum compression_level_;
  ZlibCompressionStrategyEnum compression_strategy_;
  int32_t content_length_;
  int32_t memory_level_;
  std::unordered_set<std::string> cache_control_values_;
  std::unordered_set<std::string> content_type_values_;
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

  const static uint64_t WINDOW_BITS;
};

} // namespace Http
} // namespace Envoy
