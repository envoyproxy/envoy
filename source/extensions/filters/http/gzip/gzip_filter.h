#pragma once

#include "envoy/extensions/filters/http/gzip/v3/gzip.pb.h"

#include "common/compressor/zlib_compressor_impl.h"

#include "extensions/filters/http/common/compressor/compressor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

/**
 * Configuration for the gzip filter.
 */
class GzipFilterConfig : public Common::Compressors::CompressorFilterConfig {

public:
  GzipFilterConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip,
                   const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime);

  std::unique_ptr<Compressor::Compressor> makeCompressor() override;

  Compressor::ZlibCompressorImpl::CompressionLevel compressionLevel() const {
    return compression_level_;
  }
  Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategy() const {
    return compression_strategy_;
  }

  uint64_t memoryLevel() const { return memory_level_; }
  uint64_t windowBits() const { return window_bits_; }

private:
  static Compressor::ZlibCompressorImpl::CompressionLevel compressionLevelEnum(
      envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::Enum compression_level);
  static Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategyEnum(
      envoy::extensions::filters::http::gzip::v3::Gzip::CompressionStrategy compression_strategy);

  static uint64_t memoryLevelUint(Protobuf::uint32 level);
  static uint64_t windowBitsUint(Protobuf::uint32 window_bits);

  // TODO(rojkov): this is going to be deprecated when the old configuration fields are dropped.
  static const envoy::extensions::filters::http::compressor::v3::Compressor
  compressorConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip);

  Compressor::ZlibCompressorImpl::CompressionLevel compression_level_;
  Compressor::ZlibCompressorImpl::CompressionStrategy compression_strategy_;

  int32_t memory_level_;
  int32_t window_bits_;
};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
