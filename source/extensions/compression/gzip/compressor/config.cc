#include "extensions/compression/gzip/compressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Compressor {

namespace {
// Default zlib memory level.
const uint64_t DefaultMemoryLevel = 5;

// Default and maximum compression window size.
const uint64_t DefaultWindowBits = 12;

// When logical OR'ed to window bits, this sets a gzip header and trailer around the compressed
// data.
const uint64_t GzipHeaderValue = 16;

// Default zlib chunk size.
const uint32_t DefaultChunkSize = 4096;
} // namespace

GzipCompressorFactory::GzipCompressorFactory(
    const envoy::extensions::compression::gzip::compressor::v3::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      memory_level_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, memory_level, DefaultMemoryLevel)),
      window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, window_bits, DefaultWindowBits) |
                   GzipHeaderValue),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, DefaultChunkSize)) {}

ZlibCompressorImpl::CompressionLevel GzipCompressorFactory::compressionLevelEnum(
    envoy::extensions::compression::gzip::compressor::v3::Gzip::CompressionLevel
        compression_level) {
  switch (compression_level) {
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::BEST_SPEED:
    return ZlibCompressorImpl::CompressionLevel::Speed;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_2:
    return ZlibCompressorImpl::CompressionLevel::Level2;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_3:
    return ZlibCompressorImpl::CompressionLevel::Level3;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_4:
    return ZlibCompressorImpl::CompressionLevel::Level4;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_5:
    return ZlibCompressorImpl::CompressionLevel::Level5;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_6:
    return ZlibCompressorImpl::CompressionLevel::Level6;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_7:
    return ZlibCompressorImpl::CompressionLevel::Level7;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::COMPRESSION_LEVEL_8:
    return ZlibCompressorImpl::CompressionLevel::Level8;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::BEST_COMPRESSION:
    return ZlibCompressorImpl::CompressionLevel::Best;
  default:
    return ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

ZlibCompressorImpl::CompressionStrategy GzipCompressorFactory::compressionStrategyEnum(
    envoy::extensions::compression::gzip::compressor::v3::Gzip::CompressionStrategy
        compression_strategy) {
  switch (compression_strategy) {
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::FILTERED:
    return ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::FIXED:
    return ZlibCompressorImpl::CompressionStrategy::Fixed;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::HUFFMAN_ONLY:
    return ZlibCompressorImpl::CompressionStrategy::Huffman;
  case envoy::extensions::compression::gzip::compressor::v3::Gzip::RLE:
    return ZlibCompressorImpl::CompressionStrategy::Rle;
  default:
    return ZlibCompressorImpl::CompressionStrategy::Standard;
  }
}

Envoy::Compression::Compressor::CompressorPtr GzipCompressorFactory::createCompressor() {
  auto compressor = std::make_unique<ZlibCompressorImpl>(chunk_size_);
  compressor->init(compression_level_, compression_strategy_, window_bits_, memory_level_);
  return compressor;
}

Envoy::Compression::Compressor::CompressorFactoryPtr
GzipCompressorLibraryFactory::createCompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::gzip::compressor::v3::Gzip& proto_config) {
  return std::make_unique<GzipCompressorFactory>(proto_config);
}

/**
 * Static registration for the gzip compressor library. @see NamedCompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(GzipCompressorLibraryFactory,
                 Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory);

} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
