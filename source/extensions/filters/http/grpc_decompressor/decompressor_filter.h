#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/extensions/filters/http/grpc_decompressor/v3/decompressor.pb.h"
#include "envoy/http/filter.h"

#include "source/common/grpc/codec.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcDecompressor {

/**
 * All decompressor filter stats. @see stats_macros.h
 */
#define ALL_DECOMPRESSOR_STATS(COUNTER)                                                            \
  COUNTER(decompressed)                                                                            \
  COUNTER(not_decompressed)                                                                        \
  COUNTER(total_uncompressed_bytes)                                                                \
  COUNTER(total_compressed_bytes)                                                                  \
  COUNTER(message_length_too_large)                                                                \
  COUNTER(message_decoding_error)

/**
 * Struct definition for decompressor stats. @see stats_macros.h
 */
struct CommonDecompressorStats {
  ALL_DECOMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the decompressor filter.
 */
class DecompressorFilterConfig {
public:
  class DirectionConfig {
  public:
    DirectionConfig(const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor::
                        CommonDirectionConfig& proto_config,
                    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    virtual ~DirectionConfig() = default;

    const CommonDecompressorStats& stats() const { return stats_; }
    bool decompressionEnabled() const { return decompression_enabled_.enabled(); }
    bool advertiseGrpcAcceptEncoding() const { return advertise_grpc_accept_encoding_; }
    absl::optional<uint32_t> maximumMessageLength() const { return max_message_length_; }

  private:
    static CommonDecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
      return CommonDecompressorStats{ALL_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    const CommonDecompressorStats stats_;
    const Runtime::FeatureFlag decompression_enabled_;
    const bool advertise_grpc_accept_encoding_;
    const absl::optional<uint32_t> max_message_length_;
  };

  class RequestDirectionConfig : public DirectionConfig {
  public:
    RequestDirectionConfig(const envoy::extensions::filters::http::grpc_decompressor::v3::
                               Decompressor::RequestDirectionConfig& proto_config,
                           const std::string& stats_prefix, Stats::Scope& scope,
                           Runtime::Loader& runtime);
  };

  class ResponseDirectionConfig : public DirectionConfig {
  public:
    ResponseDirectionConfig(const envoy::extensions::filters::http::grpc_decompressor::v3::
                                Decompressor::ResponseDirectionConfig& proto_config,
                            const std::string& stats_prefix, Stats::Scope& scope,
                            Runtime::Loader& runtime);
  };

  DecompressorFilterConfig(
      const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      Compression::Decompressor::DecompressorFactoryPtr decompressor_factory);

  Compression::Decompressor::DecompressorFactory& decompressorFactory() {
    return *decompressor_factory_;
  }
  const std::string& decompressorStatsPrefix() { return decompressor_stats_prefix_; }

  const std::string& contentEncoding() { return decompressor_factory_->contentEncoding(); }
  const RequestDirectionConfig& requestDirectionConfig() { return request_direction_config_; }
  const ResponseDirectionConfig& responseDirectionConfig() { return response_direction_config_; }

private:
  const std::string stats_prefix_;
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
  std::string getGrpcEncoding() const {
    return config_->contentEncoding(); // As of now, the grpc-encoding header matches what the
                                       // content encoding is for the decompressor.
  }

  template <class HeaderType> void clearGrpcEncoding(HeaderType& headers) {
    const auto handle = getGrpcEncodingHandle<HeaderType::header_map_type>();
    headers.removeInline(handle);
  }

  template <class HeaderType>
  bool shouldDecompress(const DecompressorFilterConfig::DirectionConfig& direction_config,
                        HeaderType& headers) {
    const bool should_decompress =
        direction_config.decompressionEnabled() && grpcEncodingMatches(headers);
    if (should_decompress) {
      direction_config.stats().decompressed_.inc();
      // Update headers.
      clearGrpcEncoding(headers);
    } else {
      direction_config.stats().not_decompressed_.inc();
    }

    return should_decompress;
  }

  using HeaderMapOptRef = absl::optional<std::reference_wrapper<Http::HeaderMap>>;

  /**
   * grpc-encoding matches if the header is equal to the configured encoding.
   */
  template <Http::CustomInlineHeaderRegistry::Type Type>
  static Http::CustomInlineHeaderRegistry::Handle<Type> getGrpcEncodingHandle();
  template <class HeaderType> bool grpcEncodingMatches(HeaderType& headers) const {
    const auto handle = getGrpcEncodingHandle<HeaderType::header_map_type>();
    const auto grpc_encoding = headers.getInline(handle);
    if (grpc_encoding == nullptr) {
      return false;
    }
    return absl::EqualsIgnoreCase(grpc_encoding->value().getStringView(), getGrpcEncoding());
  }

  DecompressorFilterConfigSharedPtr config_;
  bool is_grpc_{};

  bool request_decompression_enabled_{};
  Grpc::Encoder request_encoder_;
  Grpc::Decoder request_decoder_;
  std::vector<Grpc::Frame> request_frames_;

  bool response_decompression_enabled_{};
  Grpc::Encoder response_encoder_;
  Grpc::Decoder response_decoder_;
  std::vector<Grpc::Frame> response_frames_;
};

} // namespace GrpcDecompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
