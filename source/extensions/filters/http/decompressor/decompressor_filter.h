#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/http/filter.h"

#include "source/common/common/macros.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

/**
 * All decompressor filter stats. @see stats_macros.h
 */
#define ALL_DECOMPRESSOR_STATS(COUNTER)                                                            \
  COUNTER(decompressed)                                                                            \
  COUNTER(not_decompressed)                                                                        \
  COUNTER(total_uncompressed_bytes)                                                                \
  COUNTER(total_compressed_bytes)

/**
 * Struct definition for decompressor stats. @see stats_macros.h
 */
struct DecompressorStats {
  ALL_DECOMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the decompressor filter.
 */
class DecompressorFilterConfig {
public:
  class DirectionConfig {
  public:
    DirectionConfig(const envoy::extensions::filters::http::decompressor::v3::Decompressor::
                        CommonDirectionConfig& proto_config,
                    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    virtual ~DirectionConfig() = default;

    virtual const std::string& logString() const PURE;
    const DecompressorStats& stats() const { return stats_; }
    bool decompressionEnabled() const { return decompression_enabled_.enabled(); }
    bool ignoreNoTransformHeader() const { return ignore_no_transform_header_; }

  private:
    static DecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
      return DecompressorStats{ALL_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    const DecompressorStats stats_;
    const Runtime::FeatureFlag decompression_enabled_;
    const bool ignore_no_transform_header_;
  };

  class RequestDirectionConfig : public DirectionConfig {
  public:
    RequestDirectionConfig(const envoy::extensions::filters::http::decompressor::v3::Decompressor::
                               RequestDirectionConfig& proto_config,
                           const std::string& stats_prefix, Stats::Scope& scope,
                           Runtime::Loader& runtime);

    // DirectionConfig
    const std::string& logString() const override {
      CONSTRUCT_ON_FIRST_USE(std::string, "request");
    }

    bool advertiseAcceptEncoding() const { return advertise_accept_encoding_; }

  private:
    const bool advertise_accept_encoding_;
  };

  class ResponseDirectionConfig : public DirectionConfig {
  public:
    ResponseDirectionConfig(const envoy::extensions::filters::http::decompressor::v3::Decompressor::
                                ResponseDirectionConfig& proto_config,
                            const std::string& stats_prefix, Stats::Scope& scope,
                            Runtime::Loader& runtime);

    // DirectionConfig
    const std::string& logString() const override {
      CONSTRUCT_ON_FIRST_USE(std::string, "response");
    }
  };

  DecompressorFilterConfig(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      Compression::Decompressor::DecompressorFactoryPtr decompressor_factory);

  Compression::Decompressor::DecompressorPtr makeDecompressor() {
    return decompressor_factory_->createDecompressor(decompressor_stats_prefix_);
  }
  const std::string& contentEncoding() { return decompressor_factory_->contentEncoding(); }
  const RequestDirectionConfig& requestDirectionConfig() { return request_direction_config_; }
  const ResponseDirectionConfig& responseDirectionConfig() { return response_direction_config_; }
  const Http::LowerCaseString& trailersCompressedBytesString() const {
    CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, Http::LowerCaseString(fmt::format(
                                                      "{}-compressed-bytes", trailers_prefix_)));
  }
  const Http::LowerCaseString& trailersUncompressedBytesString() const {
    CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, Http::LowerCaseString(fmt::format(
                                                      "{}-uncompressed-bytes", trailers_prefix_)));
  }

private:
  const std::string stats_prefix_;
  const std::string trailers_prefix_;
  const std::string decompressor_stats_prefix_;
  const Compression::Decompressor::DecompressorFactoryPtr decompressor_factory_;
  const RequestDirectionConfig request_direction_config_;
  const ResponseDirectionConfig response_direction_config_;
};

using DecompressorFilterConfigSharedPtr = std::shared_ptr<DecompressorFilterConfig>;

/**
 * A filter that decompresses data bidirectionally.
 */
class DecompressorFilter : public Http::PassThroughFilter,
                           public Logger::Loggable<Logger::Id::filter> {
public:
  DecompressorFilter(DecompressorFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  struct ByteTracker {
    ByteTracker(const Http::LowerCaseString& compressed_bytes_trailer,
                const Http::LowerCaseString& uncompressed_bytes_trailer)
        : compressed_bytes_trailer_(compressed_bytes_trailer),
          uncompressed_bytes_trailer_(uncompressed_bytes_trailer) {}
    void chargeBytes(uint64_t compressed_bytes, uint64_t uncompressed_bytes) {
      total_compressed_bytes_ += compressed_bytes;
      total_uncompressed_bytes_ += uncompressed_bytes;
    }
    void reportTotalBytes(Http::HeaderMap& trailers) const {
      trailers.addReferenceKey(compressed_bytes_trailer_, total_compressed_bytes_);
      trailers.addReferenceKey(uncompressed_bytes_trailer_, total_uncompressed_bytes_);
    }

  private:
    const Http::LowerCaseString& compressed_bytes_trailer_;
    const Http::LowerCaseString& uncompressed_bytes_trailer_;
    uint64_t total_compressed_bytes_{};
    uint64_t total_uncompressed_bytes_{};
  };
  using ByteTrackerOptConstRef = absl::optional<std::reference_wrapper<const ByteTracker>>;

  template <class HeaderType>
  Http::FilterHeadersStatus
  maybeInitDecompress(const DecompressorFilterConfig::DirectionConfig& direction_config,
                      Compression::Decompressor::DecompressorPtr& decompressor,
                      Http::StreamFilterCallbacks& callbacks, HeaderType& headers) {
    const bool should_decompress =
        direction_config.decompressionEnabled() &&
        (!hasCacheControlNoTransform(headers) || direction_config.ignoreNoTransformHeader()) &&
        contentEncodingMatches(headers);
    if (should_decompress) {
      direction_config.stats().decompressed_.inc();
      decompressor = config_->makeDecompressor();

      // Update headers.
      headers.removeContentLength();
      modifyContentEncoding(headers);

      ENVOY_STREAM_LOG(trace, "do decompress {}: {}", callbacks, direction_config.logString(),
                       headers);
    } else {
      direction_config.stats().not_decompressed_.inc();
      ENVOY_STREAM_LOG(trace, "do not decompress {}: {}", callbacks, direction_config.logString(),
                       headers);
    }

    return Http::FilterHeadersStatus::Continue;
  }

  using HeaderMapOptRef = absl::optional<std::reference_wrapper<Http::HeaderMap>>;
  void decompress(const DecompressorFilterConfig::DirectionConfig& direction_config,
                  const Compression::Decompressor::DecompressorPtr& decompressor,
                  Http::StreamFilterCallbacks& callbacks, Buffer::Instance& input_buffer,
                  ByteTracker& byte_tracker, HeaderMapOptRef trailers) const;

  // TODO(junr03): These can be shared between compressor and decompressor.
  template <Http::CustomInlineHeaderRegistry::Type Type>
  static Http::CustomInlineHeaderRegistry::Handle<Type> getCacheControlHandle();
  template <class HeaderType> static bool hasCacheControlNoTransform(HeaderType& headers) {
    const auto handle = getCacheControlHandle<HeaderType::header_map_type>();
    return headers.getInline(handle)
               ? StringUtil::caseFindToken(
                     headers.getInlineValue(handle), ",",
                     Http::CustomHeaders::get().CacheControlValues.NoTransform)
               : false;
  }

  /**
   * Content-Encoding matches if the configured encoding is the first value in the comma-delimited
   * Content-Encoding header, regardless of spacing and casing.
   */
  template <Http::CustomInlineHeaderRegistry::Type Type>
  static Http::CustomInlineHeaderRegistry::Handle<Type> getContentEncodingHandle();
  template <class HeaderType> bool contentEncodingMatches(HeaderType& headers) const {
    const auto handle = getContentEncodingHandle<HeaderType::header_map_type>();
    if (headers.getInline(handle)) {
      absl::string_view coding =
          StringUtil::trim(StringUtil::cropRight(headers.getInlineValue(handle), ","));
      return StringUtil::CaseInsensitiveCompare()(config_->contentEncoding(), coding);
    }
    return false;
  }

  template <class HeaderType> static void modifyContentEncoding(HeaderType& headers) {
    const auto handle = getContentEncodingHandle<HeaderType::header_map_type>();
    const auto all_codings = StringUtil::trim(headers.getInlineValue(handle));
    const auto remaining_codings = StringUtil::trim(StringUtil::cropLeft(all_codings, ","));

    if (remaining_codings != all_codings) {
      headers.setInline(handle, remaining_codings);
    } else {
      headers.removeInline(handle);
    }
  }

  DecompressorFilterConfigSharedPtr config_;
  Compression::Decompressor::DecompressorPtr request_decompressor_{};
  Compression::Decompressor::DecompressorPtr response_decompressor_{};
  ByteTracker request_byte_tracker_;
  ByteTracker response_byte_tracker_;
};

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
