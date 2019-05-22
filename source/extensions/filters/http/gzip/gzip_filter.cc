#include "extensions/filters/http/gzip/gzip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

namespace {
// Default zlib memory level.
const uint64_t DefaultMemoryLevel = 5;

// Default and maximum compression window size.
const uint64_t DefaultWindowBits = 12;

// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GzipHeaderValue = 16;

} // namespace

GzipFilterConfig::GzipFilterConfig(const envoy::config::filter::http::gzip::v2::Gzip& gzip,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   Runtime::Loader& runtime)
    : CompressorFilterConfig(gzip.content_length().value(), gzip.content_type(),
                             gzip.disable_on_etag_header(), gzip.remove_accept_encoding_header(),
                             stats_prefix + "gzip.", scope, runtime, "gzip"),
      compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      memory_level_(memoryLevelUint(gzip.memory_level().value())),
      window_bits_(windowBitsUint(gzip.window_bits().value())) {}

std::unique_ptr<Compressor::Compressor> GzipFilterConfig::makeCompressor() {
  auto compressor = std::make_unique<Compressor::ZlibCompressorImpl>();
  compressor->init(compressionLevel(), compressionStrategy(), windowBits(), memoryLevel());
  return compressor;
}

Compressor::ZlibCompressorImpl::CompressionLevel GzipFilterConfig::compressionLevelEnum(
    envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum compression_level) {
  switch (compression_level) {
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum::
      Gzip_CompressionLevel_Enum_BEST:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Best;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum::
      Gzip_CompressionLevel_Enum_SPEED:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
  default:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

Compressor::ZlibCompressorImpl::CompressionStrategy GzipFilterConfig::compressionStrategyEnum(
    envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy compression_strategy) {
  switch (compression_strategy) {
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_RLE:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_FILTERED:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_HUFFMAN:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
  default:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
  }
}

uint64_t GzipFilterConfig::memoryLevelUint(Protobuf::uint32 level) {
  return level > 0 ? level : DefaultMemoryLevel;
}

uint64_t GzipFilterConfig::windowBitsUint(Protobuf::uint32 window_bits) {
  return (window_bits > 0 ? window_bits : DefaultWindowBits) | GzipHeaderValue;
}

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
