#pragma once

#include "envoy/config/filter/http/gzip/v2/gzip.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"
#include "envoy/runtime/runtime.h"

#include "common/buffer/buffer_impl.h"
#include "common/compressor/zlib_compressor_impl.h"
#include "common/http/header_map_impl.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

/**
 * Configuration for the gzip filter.
 */
class GzipFilterConfig {

public:
  GzipFilterConfig(const envoy::config::filter::http::gzip::v2::Gzip& gzip,
                   Runtime::Loader& runtime);

  Compressor::ZlibCompressorImpl::CompressionLevel compressionLevel() const {
    return compression_level_;
  }
  Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategy() const {
    return compression_strategy_;
  }

  Runtime::Loader& runtime() { return runtime_; }
  const StringUtil::CaseUnorderedSet& contentTypeValues() const { return content_type_values_; }
  bool disableOnEtagHeader() const { return disable_on_etag_header_; }
  bool removeAcceptEncodingHeader() const { return remove_accept_encoding_header_; }
  uint64_t memoryLevel() const { return memory_level_; }
  uint64_t minimumLength() const { return content_length_; }
  uint64_t windowBits() const { return window_bits_; }

private:
  static Compressor::ZlibCompressorImpl::CompressionLevel compressionLevelEnum(
      envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum compression_level);
  static Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategyEnum(
      envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy compression_strategy);
  static StringUtil::CaseUnorderedSet
  contentTypeSet(const Protobuf::RepeatedPtrField<Envoy::ProtobufTypes::String>& types);

  static uint64_t contentLengthUint(Protobuf::uint32 length);
  static uint64_t memoryLevelUint(Protobuf::uint32 level);
  static uint64_t windowBitsUint(Protobuf::uint32 window_bits);

  Compressor::ZlibCompressorImpl::CompressionLevel compression_level_;
  Compressor::ZlibCompressorImpl::CompressionStrategy compression_strategy_;

  int32_t content_length_;
  int32_t memory_level_;
  int32_t window_bits_;

  StringUtil::CaseUnorderedSet content_type_values_;

  bool disable_on_etag_header_;
  bool remove_accept_encoding_header_;
  Runtime::Loader& runtime_;
};
typedef std::shared_ptr<GzipFilterConfig> GzipFilterConfigSharedPtr;

/**
 * A filter that compresses data dispatched from the upstream upon client request.
 */
class GzipFilter : public Http::StreamFilter {
public:
  GzipFilter(const GzipFilterConfigSharedPtr& config);

  // Http::StreamFilterBase
  void onDestroy() override{};

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  // TODO(gsagula): This is here temporarily and just to facilitate testing. Ideally all
  // the logic in these private member functions would be availale in another class.
  friend class GzipFilterTest;

  bool hasCacheControlNoTransform(Http::HeaderMap& headers) const;
  bool isAcceptEncodingAllowed(Http::HeaderMap& headers) const;
  bool isContentTypeAllowed(Http::HeaderMap& headers) const;
  bool isEtagAllowed(Http::HeaderMap& headers) const;
  bool isMinimumContentLength(Http::HeaderMap& headers) const;
  bool isTransferEncodingAllowed(Http::HeaderMap& headers) const;

  void sanitizeEtagHeader(Http::HeaderMap& headers);
  void insertVaryHeader(Http::HeaderMap& headers);

  bool skip_compression_;
  Buffer::OwnedImpl compressed_data_;
  Compressor::ZlibCompressorImpl compressor_;
  GzipFilterConfigSharedPtr config_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
