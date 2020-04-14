#include "extensions/filters/http/compressor/gzip/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace Gzip {

namespace {
// Default zlib memory level.
const uint64_t DefaultMemoryLevel = 5;

// Default and maximum compression window size.
const uint64_t DefaultWindowBits = 12;

// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GzipHeaderValue = 16;

// Default zlib chunk size.
const uint32_t DefaultChunkSize = 4096;
} // namespace

GzipCompressorFactory::GzipCompressorFactory(
    const envoy::extensions::filters::http::compressor::gzip::v3::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      memory_level_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, memory_level, DefaultMemoryLevel)),
      window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, window_bits, DefaultWindowBits) |
                   GzipHeaderValue),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, DefaultChunkSize)) {}

Envoy::Compressor::ZlibCompressorImpl::CompressionLevel GzipCompressorFactory::compressionLevelEnum(
    envoy::extensions::filters::http::compressor::gzip::v3::Gzip::CompressionLevel
        compression_level) {
  switch (compression_level) {
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::BEST_SPEED:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_2:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level2;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_3:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level3;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_4:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level4;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_5:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level5;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_6:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level6;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_7:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level7;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::COMPRESSION_LEVEL_8:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level8;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::BEST_COMPRESSION:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best;
  default:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy
GzipCompressorFactory::compressionStrategyEnum(
    envoy::extensions::filters::http::compressor::gzip::v3::Gzip::CompressionStrategy
        compression_strategy) {
  switch (compression_strategy) {
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::FILTERED:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::FIXED:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::HUFFMAN_ONLY:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
  case envoy::extensions::filters::http::compressor::gzip::v3::Gzip::RLE:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
  default:
    return Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
  }
}

Envoy::Compressor::CompressorPtr GzipCompressorFactory::createCompressor() {
  auto compressor = std::make_unique<Envoy::Compressor::ZlibCompressorImpl>(chunk_size_);
  compressor->init(compression_level_, compression_strategy_, window_bits_, memory_level_);
  return compressor;
}

Compressor::CompressorFactoryPtr
GzipCompressorLibraryFactory::createCompressorLibraryFromProtoTyped(
    const envoy::extensions::filters::http::compressor::gzip::v3::Gzip& proto_config) {
  return std::make_unique<GzipCompressorFactory>(proto_config);
}

/**
 * Static registration for the gzip filter. @see NamedCompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(GzipCompressorLibraryFactory, NamedCompressorLibraryConfigFactory);

} // namespace Gzip
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
