#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/filters/http/grpc_compressor/v3/compressor.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcCompressor {

/**
 * gRPC compressor filter stats common for responses and requests. @see stats_macros.h
 * "total_uncompressed_bytes" only includes bytes from requests or responses that were marked for
 * compression. If the request (or response) was not marked for compression, the filter increments
 *  "not_compressed", but does not add to "total_uncompressed_bytes". This way, the user can
 *  measure the memory performance of the compression.
 */
#define COMMON_GRPC_COMPRESSOR_STATS(COUNTER)                                                      \
  COUNTER(compressed_rpc)                                                                          \
  COUNTER(not_compressed_rpc)                                                                      \
  COUNTER(compressed_msg)                                                                          \
  COUNTER(total_uncompressed_bytes)                                                                \
  COUNTER(total_compressed_bytes)                                                                  \
  COUNTER(message_length_too_large)                                                                \
  COUNTER(message_decoding_error)

/**
 * Struct definition for gRPC compressor stats. @see stats_macros.h
 */
struct CommonCompressorStats {
  COMMON_GRPC_COMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the gRPC compressor filter.
 */
class CompressorFilterConfig {
public:
  class DirectionConfig {
  public:
    DirectionConfig(const envoy::extensions::filters::http::grpc_compressor::v3::Compressor::
                        CommonDirectionConfig& proto_config,
                    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    virtual ~DirectionConfig() = default;

    virtual bool compressionEnabled() const PURE;

    const CommonCompressorStats& stats() const { return stats_; }
    bool removeGrpcAcceptEncoding() const { return remove_grpc_accept_encoding_; }
    uint32_t minimumMessageLength() const { return min_message_length_; }
    absl::optional<uint32_t> maximumMessageLength() const { return max_message_length_; }

  protected:
    const Runtime::FeatureFlag compression_enabled_;

  private:
    static CommonCompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
      return CommonCompressorStats{
          COMMON_GRPC_COMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    const bool remove_grpc_accept_encoding_;
    const uint32_t min_message_length_;
    const absl::optional<uint32_t> max_message_length_;
    const CommonCompressorStats stats_;
  };

  class RequestDirectionConfig : public DirectionConfig {
  public:
    RequestDirectionConfig(
        const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
        const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    bool compressionEnabled() const override { return is_set_ && compression_enabled_.enabled(); }

  private:
    const bool is_set_;
  };

  class ResponseDirectionConfig : public DirectionConfig {
  public:
    ResponseDirectionConfig(
        const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
        const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

    bool compressionEnabled() const override { return is_set_ && compression_enabled_.enabled(); }

  private:
    const bool is_set_;
  };

  CompressorFilterConfig(
      const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory);

  const std::string grpcEncoding() const { return grpc_encoding_; };
  const RequestDirectionConfig& requestDirectionConfig() { return request_direction_config_; }
  const ResponseDirectionConfig& responseDirectionConfig() { return response_direction_config_; }
  Envoy::Compression::Compressor::CompressorFactory& compressorFactory() const {
    return *compressor_factory_;
  }

private:
  const std::string common_stats_prefix_;
  const RequestDirectionConfig request_direction_config_;
  const ResponseDirectionConfig response_direction_config_;
  const std::string grpc_encoding_;
  const Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory_;
};
using CompressorFilterConfigSharedPtr = std::shared_ptr<CompressorFilterConfig>;

using CompressorFactoryConstOptRef =
    OptRef<const Envoy::Compression::Compressor::CompressorFactory>;

using DirectionConfigOptRef = OptRef<const CompressorFilterConfig::DirectionConfig>;

/**
 * A filter that optionallycompresses data both in request and response directions.
 */
class CompressorFilter : public Http::PassThroughFilter,
                         public Logger::Loggable<Logger::Id::filter> {
public:
  explicit CompressorFilter(const CompressorFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  bool compressionEnabled(const DirectionConfigOptRef config) const;

  // Returns the appropriate grpc encoding for the current route.
  std::string getGrpcEncoding() const;

  template <Http::CustomInlineHeaderRegistry::Type Type>
  static Http::CustomInlineHeaderRegistry::Handle<Type> getGrpcEncodingHandle();

  /**
   * grpc-encoding matches if the header is not present or the value is the identity encoding.
   */
  template <class HeaderType> bool isGrpcEncodingAllowed(HeaderType& headers) const {
    const auto handle = getGrpcEncodingHandle<HeaderType::header_map_type>();
    const auto grpc_encoding = headers.getInline(handle);
    if (grpc_encoding == nullptr) {
      return true;
    }
    return absl::EqualsIgnoreCase(grpc_encoding->value().getStringView(),
                                  Http::CustomHeaders::get().AcceptEncodingValues.Identity);
  }
  bool connectionAllowsGrpcEncoding() const;

  /**
   * grpc-accept-encoding matches if one of the values in the header is the configured encoding.
   */
  bool isGrpcAcceptEncodingAllowed(const absl::string_view grpc_accept_encoding) const;

  /**
   * Remove this filter's encoding from the grpc-accept-encoding header if it exists.
   */
  template <Http::CustomInlineHeaderRegistry::Type Type>
  static Http::CustomInlineHeaderRegistry::Handle<Type> getGrpcAcceptEncodingHandle();
  template <class HeaderType> void removeGrpcAcceptEncoding(HeaderType& headers) const {
    const auto handle = getGrpcAcceptEncodingHandle<HeaderType::header_map_type>();
    const auto grpc_accept_encoding = headers.getInline(handle);
    if (grpc_accept_encoding == nullptr) {
      return;
    }
    std::vector<absl::string_view> remaining_values;
    for (absl::string_view header_value :
         StringUtil::splitToken(grpc_accept_encoding->value().getStringView(), ",")) {
      const auto trimmed_value = StringUtil::trim(header_value);
      if (!absl::EqualsIgnoreCase(trimmed_value, getGrpcEncoding())) {
        remaining_values.push_back(trimmed_value);
      }
    }
    if (!remaining_values.empty()) {
      headers.setInline(handle, absl::StrJoin(remaining_values, ","));
    } else {
      headers.removeInline(handle);
    }
  }

  const CompressorFilterConfigSharedPtr config_;
  bool is_grpc_{};
  // This header sourced from the request headers determines the encoding allowed for the response.
  absl::optional<std::string> request_grpc_accept_encoding_;

  bool response_compression_enabled_{};
  Grpc::Decoder request_decoder_;
  Grpc::Encoder request_encoder_;
  // Keep vector of frames around to preserve vector allocation and avoid reallocation.
  std::vector<Grpc::Frame> request_frames_;

  bool request_compression_enabled_{};
  Grpc::Decoder response_decoder_;
  Grpc::Encoder response_encoder_;
  // Keep vector of frames around to preserve vector allocation and avoid reallocation.
  std::vector<Grpc::Frame> response_frames_;
};
} // namespace GrpcCompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
