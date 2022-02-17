#include "source/extensions/compression/brotli/decompressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

namespace {

const uint32_t DefaultChunkSize = 4096;

} // namespace

BrotliDecompressorFactory::BrotliDecompressorFactory(
    const envoy::extensions::compression::brotli::decompressor::v3::Brotli& brotli,
    Stats::Scope& scope)
    : scope_(scope),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(brotli, chunk_size, DefaultChunkSize)),
      disable_ring_buffer_reallocation_{brotli.disable_ring_buffer_reallocation()} {}

Envoy::Compression::Decompressor::DecompressorPtr
BrotliDecompressorFactory::createDecompressor(const std::string& stats_prefix) {
  return std::make_unique<BrotliDecompressorImpl>(scope_, stats_prefix, chunk_size_,
                                                  disable_ring_buffer_reallocation_);
}

Envoy::Compression::Decompressor::DecompressorFactoryPtr
BrotliDecompressorLibraryFactory::createDecompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::brotli::decompressor::v3::Brotli& proto_config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<BrotliDecompressorFactory>(proto_config, context.scope());
}

/**
 * Static registration for the brotli decompressor. @see NamedDecompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(BrotliDecompressorLibraryFactory,
                 Envoy::Compression::Decompressor::NamedDecompressorLibraryConfigFactory);
} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
