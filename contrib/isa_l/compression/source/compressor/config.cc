#include "contrib/isa_l/compression/source/compressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Compressor {

namespace {
// Default compression window size.
const uint64_t DefaultWindowBits = 0;

// Default chunk size.
const uint32_t DefaultChunkSize = 4096;
} // namespace

IgzipCompressorFactory::IgzipCompressorFactory(
    const envoy::extensions::compression::compressor::igzip::v3alpha::Igzip& igzip)
    : compression_level_(compressionLevelEnum(igzip.compression_level())),
      // memory_level_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, memory_level, DefaultMemoryLevel)),
      window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(igzip, window_bits, DefaultWindowBits)),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(igzip, chunk_size, DefaultChunkSize)) {}

IgzipCompressorImpl::CompressionLevel IgzipCompressorFactory::compressionLevelEnum(
   envoy::extensions::compression::compressor::igzip::v3alpha::Igzip::CompressionLevel
        compression_level) {
  switch (compression_level) {
  case envoy::extensions::compression::compressor::igzip::v3alpha::Igzip::COMPRESSION_LEVEL_1:
    return IgzipCompressorImpl::CompressionLevel::Level1;
  case envoy::extensions::compression::compressor::igzip::v3alpha::Igzip::COMPRESSION_LEVEL_3:
    return IgzipCompressorImpl::CompressionLevel::Level3;
  default:
    return IgzipCompressorImpl::CompressionLevel::Level2;
  }
}

Envoy::Compression::Compressor::CompressorPtr IgzipCompressorFactory::createCompressor() {
  auto compressor = std::make_unique<IgzipCompressorImpl>(chunk_size_);
  compressor->init(compression_level_, window_bits_);
  return compressor;
}

Envoy::Compression::Compressor::CompressorFactoryPtr
IgzipCompressorLibraryFactory::createCompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::compressor::igzip::v3alpha::Igzip& proto_config,
    Server::Configuration::FactoryContext&) {
  return std::make_unique<IgzipCompressorFactory>(proto_config);
}

/**
 * Static registration for the igzip compressor library. @see NamedCompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(IgzipCompressorLibraryFactory,
                 Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory);

} // namespace Compressor
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
