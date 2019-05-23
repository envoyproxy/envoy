#pragma once

#include "envoy/compressor/compressor.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Compressors {

/**
 * All compressor filter stats. @see stats_macros.h
 * "total_uncompressed_bytes" only includes bytes
 * from requests that were marked for compression.
 * If the request was not marked for compression,
 * the filter increments "not_compressed", but does
 * not add to "total_uncompressed_bytes". This way,
 * the user can measure the memory performance of the
 * compression.
 * "header_gzip" is specific to the gzip filter and is
 * deprecated since it duplicates "header_compressor_used".
 */
// clang-format off
#define ALL_COMPRESSOR_STATS(COUNTER)    \
  COUNTER(compressed)                    \
  COUNTER(not_compressed)                \
  COUNTER(no_accept_header)              \
  COUNTER(header_identity)               \
  COUNTER(header_gzip)                   \
  COUNTER(header_compressor_used)        \
  COUNTER(header_compressor_overshadowed)\
  COUNTER(header_wildcard)               \
  COUNTER(header_not_valid)              \
  COUNTER(total_uncompressed_bytes)      \
  COUNTER(total_compressed_bytes)        \
  COUNTER(content_length_too_small)      \
  COUNTER(not_compressed_etag)           \
// clang-format on

/**
 * Struct definition for compressor stats. @see stats_macros.h
 */
struct CompressorStats {
  ALL_COMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

class CompressorFilterConfig {

public:
  CompressorFilterConfig() = delete;

  virtual const std::string featureName() const PURE;
  virtual std::unique_ptr<Compressor::Compressor> makeCompressor() PURE;

  Runtime::Loader& runtime() { return runtime_; }
  CompressorStats& stats() { return stats_; }
  const StringUtil::CaseUnorderedSet& contentTypeValues() const { return content_type_values_; }
  bool disableOnEtagHeader() const { return disable_on_etag_header_; }
  bool removeAcceptEncodingHeader() const { return remove_accept_encoding_header_; }
  uint32_t minimumLength() const { return content_length_; }
  const std::string contentEncoding() const { return content_encoding_; };
  const std::vector<std::string> registeredCompressors() const { return registered_compressors_; }

  virtual ~CompressorFilterConfig();

protected:
  CompressorFilterConfig(const Protobuf::uint32 content_length,
                         const Protobuf::RepeatedPtrField<std::string>& content_types,
                         const bool disable_on_etag_header,
                         const bool remove_accept_encoding_header,
                         const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
                         const std::string& content_encoding);

private:
  static StringUtil::CaseUnorderedSet
  contentTypeSet(const Protobuf::RepeatedPtrField<std::string>& types);

  static uint32_t contentLengthUint(Protobuf::uint32 length);

  static CompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CompressorStats{ALL_COMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  uint32_t content_length_;

  StringUtil::CaseUnorderedSet content_type_values_;
  bool disable_on_etag_header_;
  bool remove_accept_encoding_header_;
  CompressorStats stats_;
  Runtime::Loader& runtime_;
  std::string content_encoding_;
  static std::vector<std::string> registered_compressors_;
};
using CompressorFilterConfigSharedPtr = std::shared_ptr<CompressorFilterConfig>;

/**
 * A filter that compresses data dispatched from the upstream upon client request.
 */
class CompressorFilter : public Http::StreamFilter {
public:
  CompressorFilter(CompressorFilterConfigSharedPtr config);

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
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  // TODO(gsagula): This is here temporarily and just to facilitate testing. Ideally all
  // the logic in these private member functions would be available in another class.
  friend class CompressorFilterTest;

  bool hasCacheControlNoTransform(Http::HeaderMap& headers) const;
  bool isAcceptEncodingAllowed(const Http::HeaderMap& headers) const;
  bool isContentTypeAllowed(Http::HeaderMap& headers) const;
  bool isEtagAllowed(Http::HeaderMap& headers) const;
  bool isMinimumContentLength(Http::HeaderMap& headers) const;
  bool isTransferEncodingAllowed(Http::HeaderMap& headers) const;

  void sanitizeEtagHeader(Http::HeaderMap& headers);
  void insertVaryHeader(Http::HeaderMap& headers);

  bool skip_compression_;
  Buffer::OwnedImpl compressed_data_;
  std::unique_ptr<Compressor::Compressor> compressor_;
  CompressorFilterConfigSharedPtr config_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy