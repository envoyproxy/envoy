#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/http/filter.h"

#include "common/common/macros.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/common/pass_through_filter.h"

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

  private:
    static DecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
      return DecompressorStats{ALL_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
    }

    const DecompressorStats stats_;
    const Runtime::FeatureFlag decompression_enabled_;
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
    return decompressor_factory_->createDecompressor();
  }
  const std::string& contentEncoding() { return decompressor_factory_->contentEncoding(); }
  const RequestDirectionConfig& requestDirectionConfig() { return request_direction_config_; }
  const ResponseDirectionConfig& responseDirectionConfig() { return response_direction_config_; }

private:
  const std::string stats_prefix_;
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

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;

private:
  Http::FilterHeadersStatus
  maybeInitDecompress(const DecompressorFilterConfig::DirectionConfig& direction_config,
                      Compression::Decompressor::DecompressorPtr& decompressor,
                      Http::StreamFilterCallbacks& callbacks,
                      Http::RequestOrResponseHeaderMap& headers);

  Http::FilterDataStatus
  maybeDecompress(const DecompressorFilterConfig::DirectionConfig& direction_config,
                  const Compression::Decompressor::DecompressorPtr& decompressor,
                  Http::StreamFilterCallbacks& callbacks, Buffer::Instance& input_buffer) const;

  // TODO(junr03): these do not need to be member functions. They can all be part of a static
  // utility class. Moreover, they can be shared between compressor and decompressor.
  bool hasCacheControlNoTransform(Http::RequestOrResponseHeaderMap& headers) const;
  bool contentEncodingMatches(Http::RequestOrResponseHeaderMap& headers) const;
  void modifyContentEncoding(Http::RequestOrResponseHeaderMap& headers) const;

  DecompressorFilterConfigSharedPtr config_;
  Compression::Decompressor::DecompressorPtr request_decompressor_{};
  Compression::Decompressor::DecompressorPtr response_decompressor_{};
};

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy