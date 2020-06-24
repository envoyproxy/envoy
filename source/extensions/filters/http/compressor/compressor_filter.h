#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"

#include "extensions/filters/http/common/compressor/compressor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

/**
 * Configuration for the compressor filter.
 */
class CompressorFilterConfig : public Common::Compressors::CompressorFilterConfig {
  // TODO(rojkov): move functionality of Common::Compressors::CompressorFilterConfig
  // to this class when `envoy.filters.http.gzip` is fully deprecated and dropped.
public:
  CompressorFilterConfig() = delete;
  CompressorFilterConfig(
      const envoy::extensions::filters::http::compressor::v3::Compressor& genereic_compressor,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory);

  Envoy::Compression::Compressor::CompressorPtr makeCompressor() override;

private:
  const Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory_;
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
