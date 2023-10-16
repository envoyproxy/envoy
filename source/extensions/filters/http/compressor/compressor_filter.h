#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

/**
 * Compressor filter stats common for responses and requests. @see stats_macros.h
 * "total_uncompressed_bytes" only includes bytes from requests or responses that were marked for
 * compression. If the request (or response) was not marked for compression, the filter increments
 *  "not_compressed", but does not add to "total_uncompressed_bytes". This way, the user can
 *  measure the memory performance of the compression.
 */
#define COMMON_COMPRESSOR_STATS(COUNTER)                                                           \
  COUNTER(compressed)                                                                              \
  COUNTER(not_compressed)                                                                          \
  COUNTER(total_uncompressed_bytes)                                                                \
  COUNTER(total_compressed_bytes)                                                                  \
  COUNTER(content_length_too_small)

/**
 * Compressor filter stats specific to responses only. @see stats_macros.h
 * "header_compressor_used" is a number of requests whose Accept-Encoding header explicitly stated
 * that the response body should be compressed with the encoding provided by this filter instance.
 *
 * "header_compressor_overshadowed" is a number of requests skipped by this filter instance because
 * they were handled by another filter in the same filter chain.
 *
 * "header_gzip" is specific to the gzip filter and is deprecated since it duplicates
 * "header_compressor_used".
 */
#define RESPONSE_COMPRESSOR_STATS(COUNTER)                                                         \
  COUNTER(no_accept_header)                                                                        \
  COUNTER(header_identity)                                                                         \
  COUNTER(header_compressor_used)                                                                  \
  COUNTER(header_compressor_overshadowed)                                                          \
  COUNTER(header_wildcard)                                                                         \
  COUNTER(header_not_valid)                                                                        \
  COUNTER(not_compressed_etag)

/**
 * Struct definitions for compressor stats. @see stats_macros.h
 */
struct CompressorStats {
  COMMON_COMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};
struct ResponseCompressorStats {
  RESPONSE_COMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the compressor filter.
 */
class CompressorFilterConfig {
public:
  class DirectionConfig {
  public:
    DirectionConfig(
        const envoy::extensions::filters::http::compressor::v3::Compressor::CommonDirectionConfig&
            proto_config,
        const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    virtual ~DirectionConfig() = default;

    virtual bool compressionEnabled() const PURE;

    const CompressorStats& stats() const { return stats_; }
    const StringUtil::CaseUnorderedSet& contentTypeValues() const { return content_type_values_; }
    uint32_t minimumLength() const { return min_content_length_; }
    bool isMinimumContentLength(const Http::RequestOrResponseHeaderMap& headers) const;
    bool isContentTypeAllowed(const Http::RequestOrResponseHeaderMap& headers) const;

  protected:
    const Runtime::FeatureFlag compression_enabled_;

  private:
    static CompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
      return CompressorStats{COMMON_COMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    static uint32_t contentLengthUint(Protobuf::uint32 length);

    static StringUtil::CaseUnorderedSet
    contentTypeSet(const Protobuf::RepeatedPtrField<std::string>& types);

    const uint32_t min_content_length_;
    const StringUtil::CaseUnorderedSet content_type_values_;
    const CompressorStats stats_;
  };

  class RequestDirectionConfig : public DirectionConfig {
  public:
    RequestDirectionConfig(
        const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
        const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    bool compressionEnabled() const override { return is_set_ && compression_enabled_.enabled(); }

  private:
    const bool is_set_;
  };

  class ResponseDirectionConfig : public DirectionConfig {
  public:
    ResponseDirectionConfig(
        const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
        const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    bool compressionEnabled() const override { return compression_enabled_.enabled(); }
    const ResponseCompressorStats& responseStats() const { return response_stats_; }
    bool disableOnEtagHeader() const { return disable_on_etag_header_; }
    bool removeAcceptEncodingHeader() const { return remove_accept_encoding_header_; }

  private:
    static ResponseCompressorStats generateResponseStats(const std::string& prefix,
                                                         Stats::Scope& scope) {
      return ResponseCompressorStats{RESPONSE_COMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    // TODO(rojkov): delete this translation function once the deprecated fields
    // are removed from envoy::extensions::filters::http::compressor::v3::Compressor.
    static const envoy::extensions::filters::http::compressor::v3::Compressor::CommonDirectionConfig
    commonConfig(const envoy::extensions::filters::http::compressor::v3::Compressor&);

    const bool disable_on_etag_header_;
    const bool remove_accept_encoding_header_;
    const ResponseCompressorStats response_stats_;
  };

  CompressorFilterConfig() = delete;
  CompressorFilterConfig(
      const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory);

  Envoy::Compression::Compressor::CompressorPtr makeCompressor();

  const std::string contentEncoding() const { return content_encoding_; };
  bool chooseFirst() const { return choose_first_; };
  const RequestDirectionConfig& requestDirectionConfig() { return request_direction_config_; }
  const ResponseDirectionConfig& responseDirectionConfig() { return response_direction_config_; }

private:
  const std::string common_stats_prefix_;
  const RequestDirectionConfig request_direction_config_;
  const ResponseDirectionConfig response_direction_config_;

  const std::string content_encoding_;
  const Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory_;
  const bool choose_first_;
};
using CompressorFilterConfigSharedPtr = std::shared_ptr<CompressorFilterConfig>;

class CompressorPerRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  CompressorPerRouteFilterConfig(
      const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& config);

  // If a value is present, that value overrides
  // ResponseDirectionConfig::compressionEnabled.
  absl::optional<bool> responseCompressionEnabled() const { return response_compression_enabled_; }
  absl::optional<bool> removeAcceptEncodingHeader() const { return remove_accept_encoding_header_; }

private:
  absl::optional<bool> response_compression_enabled_;
  absl::optional<bool> remove_accept_encoding_header_;
};

/**
 * A filter that compresses data dispatched from the upstream upon client request.
 */
class CompressorFilter : public Http::PassThroughFilter {
public:
  explicit CompressorFilter(const CompressorFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  bool compressionEnabled(const CompressorFilterConfig::ResponseDirectionConfig& config,
                          const CompressorPerRouteFilterConfig* per_route_config) const;
  bool removeAcceptEncodingHeader(const CompressorFilterConfig::ResponseDirectionConfig& config,
                                  const CompressorPerRouteFilterConfig* per_route_config) const;
  bool hasCacheControlNoTransform(Http::ResponseHeaderMap& headers) const;
  bool isAcceptEncodingAllowed(bool maybe_compress, const Http::ResponseHeaderMap& headers) const;
  bool isEtagAllowed(Http::ResponseHeaderMap& headers) const;
  bool isTransferEncodingAllowed(Http::RequestOrResponseHeaderMap& headers) const;

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

  struct CompressorInChain {
    uint32_t registration_count_;
    bool choose_first_;
  };

  std::unique_ptr<EncodingDecision> chooseEncoding(const Http::ResponseHeaderMap& headers) const;
  bool shouldCompress(const EncodingDecision& decision) const;

  Envoy::Compression::Compressor::CompressorPtr response_compressor_;
  Envoy::Compression::Compressor::CompressorPtr request_compressor_;
  const CompressorFilterConfigSharedPtr config_;
  std::unique_ptr<std::string> accept_encoding_;
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
