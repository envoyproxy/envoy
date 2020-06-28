#pragma once

#include "envoy/extensions/filters/http/gzip/v3/gzip.pb.h"

#include "extensions/compression/gzip/compressor/zlib_compressor_impl.h"
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

  Envoy::Compression::Compressor::CompressorPtr makeCompressor() override;

  Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel compressionLevel() const {
    return compression_level_;
  }
  Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy
  compressionStrategy() const {
    return compression_strategy_;
  }

  uint64_t memoryLevel() const { return memory_level_; }
  uint64_t windowBits() const { return window_bits_; }
  uint32_t chunkSize() const { return chunk_size_; }

private:
  static Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel compressionLevelEnum(
      envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::Enum compression_level);
  static Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy
  compressionStrategyEnum(
      envoy::extensions::filters::http::gzip::v3::Gzip::CompressionStrategy compression_strategy);

  static uint64_t memoryLevelUint(Protobuf::uint32 level);
  static uint64_t windowBitsUint(Protobuf::uint32 window_bits);

  // TODO(rojkov): this is going to be deprecated when the old configuration fields are dropped.
  static const envoy::extensions::filters::http::compressor::v3::Compressor
  compressorConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip);

  Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel compression_level_;
  Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy compression_strategy_;

  const int32_t memory_level_;
  const int32_t window_bits_;
  const uint32_t chunk_size_;
};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
