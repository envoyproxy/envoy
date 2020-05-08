#pragma once

#include "envoy/compression/compressor/compressor.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"

#include "common/protobuf/protobuf.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Compressors {

/**
 * All compressor filter stats. @see stats_macros.h
 * "total_uncompressed_bytes" only includes bytes from requests that were marked for compression.
 * If the request was not marked for compression, the filter increments "not_compressed", but does
 * not add to "total_uncompressed_bytes". This way, the user can measure the memory performance of
 * the compression.
 *
 * "header_compressor_used" is a number of requests whose Accept-Encoding header explicitly stated
 * that the response body should be compressed with the encoding provided by this filter instance.
 *
 * "header_compressor_overshadowed" is a number of requests skipped by this filter instance because
 * they were handled by another filter in the same filter chain.
 *
 * "header_gzip" is specific to the gzip filter and is deprecated since it duplicates
 * "header_compressor_used".
 */
#define ALL_COMPRESSOR_STATS(COUNTER)                                                              \
  COUNTER(compressed)                                                                              \
  COUNTER(not_compressed)                                                                          \
  COUNTER(no_accept_header)                                                                        \
  COUNTER(header_identity)                                                                         \
  COUNTER(header_gzip)                                                                             \
  COUNTER(header_compressor_used)                                                                  \
  COUNTER(header_compressor_overshadowed)                                                          \
  COUNTER(header_wildcard)                                                                         \
  COUNTER(header_not_valid)                                                                        \
  COUNTER(total_uncompressed_bytes)                                                                \
  COUNTER(total_compressed_bytes)                                                                  \
  COUNTER(content_length_too_small)                                                                \
  COUNTER(not_compressed_etag)

/**
 * Struct definition for compressor stats. @see stats_macros.h
 */
struct CompressorStats {
  ALL_COMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

// TODO(rojkov): merge this class with Compressor::CompressorFilterConfig when the filter
// `envoy.filters.http.gzip` is fully deprecated and dropped.
class CompressorFilterConfig {
public:
  CompressorFilterConfig() = delete;
  virtual ~CompressorFilterConfig() = default;

  virtual Envoy::Compression::Compressor::CompressorPtr makeCompressor() PURE;

  bool enabled() const { return enabled_.enabled(); }
  const CompressorStats& stats() { return stats_; }
  const StringUtil::CaseUnorderedSet& contentTypeValues() const { return content_type_values_; }
  bool disableOnEtagHeader() const { return disable_on_etag_header_; }
  bool removeAcceptEncodingHeader() const { return remove_accept_encoding_header_; }
  uint32_t minimumLength() const { return content_length_; }
  const std::string contentEncoding() const { return content_encoding_; };

protected:
  CompressorFilterConfig(
      const envoy::extensions::filters::http::compressor::v3::Compressor& compressor,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      const std::string& content_encoding);

private:
  static StringUtil::CaseUnorderedSet
  contentTypeSet(const Protobuf::RepeatedPtrField<std::string>& types);

  static uint32_t contentLengthUint(Protobuf::uint32 length);

  static CompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CompressorStats{ALL_COMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  const uint32_t content_length_;
  const StringUtil::CaseUnorderedSet content_type_values_;
  const bool disable_on_etag_header_;
  const bool remove_accept_encoding_header_;

  const CompressorStats stats_;
  Runtime::FeatureFlag enabled_;
  const std::string content_encoding_;
};
using CompressorFilterConfigSharedPtr = std::shared_ptr<CompressorFilterConfig>;

/**
 * A filter that compresses data dispatched from the upstream upon client request.
 */
class CompressorFilter : public Http::PassThroughFilter {
public:
  explicit CompressorFilter(const CompressorFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  // TODO(gsagula): This is here temporarily and just to facilitate testing. Ideally all
  // the logic in these private member functions would be available in another class.
  friend class CompressorFilterTest;

  bool hasCacheControlNoTransform(Http::ResponseHeaderMap& headers) const;
  bool isAcceptEncodingAllowed(const Http::ResponseHeaderMap& headers) const;
  bool isContentTypeAllowed(Http::ResponseHeaderMap& headers) const;
  bool isEtagAllowed(Http::ResponseHeaderMap& headers) const;
  bool isMinimumContentLength(Http::ResponseHeaderMap& headers) const;
  bool isTransferEncodingAllowed(Http::ResponseHeaderMap& headers) const;

  void sanitizeEtagHeader(Http::ResponseHeaderMap& headers);
  void insertVaryHeader(Http::ResponseHeaderMap& headers);

  class EncodingDecision : public StreamInfo::FilterState::Object {
  public:
    enum class HeaderStat { NotValid, Identity, Wildcard, ValidCompressor };
    EncodingDecision(const std::string& encoding, const HeaderStat stat)
        : encoding_(encoding), stat_(stat) {}
    const std::string& encoding() const { return encoding_; }
    HeaderStat stat() const { return stat_; }

  private:
    const std::string encoding_;
    const HeaderStat stat_;
  };

  std::unique_ptr<EncodingDecision> chooseEncoding(const Http::ResponseHeaderMap& headers) const;
  bool shouldCompress(const EncodingDecision& decision) const;

  bool skip_compression_;
  Envoy::Compression::Compressor::CompressorPtr compressor_;
  const CompressorFilterConfigSharedPtr config_;
  std::unique_ptr<std::string> accept_encoding_;
};

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
