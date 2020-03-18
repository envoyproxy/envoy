#include "extensions/filters/http/gzip/gzip_filter.h"

#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

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

GzipFilterConfig::GzipFilterConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   Runtime::Loader& runtime)
    : CompressorFilterConfig(compressorConfig(gzip), stats_prefix + "gzip.", scope, runtime,
                             Http::Headers::get().ContentEncodingValues.Gzip),
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
    envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::Enum compression_level) {
  switch (compression_level) {
  case envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::BEST:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Best;
  case envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::SPEED:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
  default:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

Compressor::ZlibCompressorImpl::CompressionStrategy GzipFilterConfig::compressionStrategyEnum(
    envoy::extensions::filters::http::gzip::v3::Gzip::CompressionStrategy compression_strategy) {
  switch (compression_strategy) {
  case envoy::extensions::filters::http::gzip::v3::Gzip::RLE:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
  case envoy::extensions::filters::http::gzip::v3::Gzip::FILTERED:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::extensions::filters::http::gzip::v3::Gzip::HUFFMAN:
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

const envoy::extensions::filters::http::compressor::v3::Compressor
GzipFilterConfig::compressorConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip) {
  if (gzip.has_compressor()) {
    return gzip.compressor();
  }
  envoy::extensions::filters::http::compressor::v3::Compressor compressor = {};
  if (gzip.has_hidden_envoy_deprecated_content_length()) {
    compressor.set_allocated_content_length(
        new Protobuf::UInt32Value(gzip.hidden_envoy_deprecated_content_length()));
  }
  for (const std::string& ctype : gzip.hidden_envoy_deprecated_content_type()) {
    compressor.add_content_type(ctype);
  }
  compressor.set_disable_on_etag_header(gzip.hidden_envoy_deprecated_disable_on_etag_header());
  compressor.set_remove_accept_encoding_header(
      gzip.hidden_envoy_deprecated_remove_accept_encoding_header());
  return compressor;
}

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
