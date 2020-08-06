#include "extensions/filters/http/gzip/gzip_filter.h"

#include "envoy/config/core/v3/base.pb.h"

#include "common/http/headers.h"
#include "common/protobuf/message_validator_impl.h"
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

// When logical OR'ed to window bits, this sets a gzip header and trailer around the compressed
// data.
const uint64_t GzipHeaderValue = 16;

} // namespace

GzipFilterConfig::GzipFilterConfig(const envoy::extensions::filters::http::gzip::v3::Gzip& gzip,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   Runtime::Loader& runtime)
    : CompressorFilterConfig(compressorConfig(gzip), stats_prefix + "gzip.", scope, runtime,
                             Http::CustomHeaders::get().ContentEncodingValues.Gzip),
      compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      memory_level_(memoryLevelUint(gzip.memory_level().value())),
      window_bits_(windowBitsUint(gzip.window_bits().value())),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, 4096)) {}

Envoy::Compression::Compressor::CompressorPtr GzipFilterConfig::makeCompressor() {
  auto compressor =
      std::make_unique<Compression::Gzip::Compressor::ZlibCompressorImpl>(chunk_size_);
  compressor->init(compressionLevel(), compressionStrategy(), windowBits(), memoryLevel());
  return compressor;
}

Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel
GzipFilterConfig::compressionLevelEnum(
    envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::Enum compression_level) {
  switch (compression_level) {
  case envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::BEST:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best;
  case envoy::extensions::filters::http::gzip::v3::Gzip::CompressionLevel::SPEED:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
  default:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy
GzipFilterConfig::compressionStrategyEnum(
    envoy::extensions::filters::http::gzip::v3::Gzip::CompressionStrategy compression_strategy) {
  switch (compression_strategy) {
  case envoy::extensions::filters::http::gzip::v3::Gzip::RLE:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
  case envoy::extensions::filters::http::gzip::v3::Gzip::FILTERED:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::extensions::filters::http::gzip::v3::Gzip::HUFFMAN:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
  default:
    return Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
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
        // According to
        // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated#embeddedmessage
        // the message Compressor takes ownership of the allocated Protobuf::Uint32Value object.
        new Protobuf::UInt32Value(gzip.hidden_envoy_deprecated_content_length()));
  }
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
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
