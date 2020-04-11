#include "extensions/decompressors/gzip/config.h"

namespace Envoy {
namespace Extensions {
namespace Decompressors {
namespace Gzip {

namespace {
const uint32_t DefaultWindowBits = 12;
const uint32_t DefaultChunkSize = 4096;
} // namespace

GzipDecompressorFactory::GzipDecompressorFactory(
    const envoy::extensions::decompressors::gzip::v3::Gzip& gzip)
    : window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, window_bits, DefaultWindowBits)),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, DefaultChunkSize)) {}

Decompressor::DecompressorPtr GzipDecompressorFactory::createDecompressor() {
  auto decompressor = std::make_unique<ZlibDecompressor>(chunk_size_);
  decompressor->init(window_bits_);
  return decompressor;
}

Decompressor::DecompressorFactoryPtr
GzipDecompressorLibraryFactory::createDecompressorLibraryFromProtoTyped(
    const envoy::extensions::decompressors::gzip::v3::Gzip& proto_config) {
  return std::make_unique<GzipDecompressorFactory>(proto_config);
}

/**
 * Static registration for the gzip filter. @see NamedDecompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(GzipDecompressorLibraryFactory,
                 Decompressor::NamedDecompressorLibraryConfigFactory);

} // namespace Gzip
} // namespace Decompressors
} // namespace Extensions
} // namespace Envoy
