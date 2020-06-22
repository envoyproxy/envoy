#include "extensions/compression/gzip/decompressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

namespace {
const uint32_t DefaultWindowBits = 12;
const uint32_t DefaultChunkSize = 4096;
// When logical OR'ed to window bits, this tells zlib library to decompress gzip data per:
// inflateInit2 in https://www.zlib.net/manual.html
const uint32_t GzipHeaderValue = 16;
} // namespace

GzipDecompressorFactory::GzipDecompressorFactory(
    const envoy::extensions::compression::gzip::decompressor::v3::Gzip& gzip)
    : window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, window_bits, DefaultWindowBits) |
                   GzipHeaderValue),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, DefaultChunkSize)) {}

Envoy::Compression::Decompressor::DecompressorPtr GzipDecompressorFactory::createDecompressor() {
  auto decompressor = std::make_unique<ZlibDecompressorImpl>(chunk_size_);
  decompressor->init(window_bits_);
  return decompressor;
}

Envoy::Compression::Decompressor::DecompressorFactoryPtr
GzipDecompressorLibraryFactory::createDecompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::gzip::decompressor::v3::Gzip& proto_config) {
  return std::make_unique<GzipDecompressorFactory>(proto_config);
}

/**
 * Static registration for the gzip decompressor. @see NamedDecompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(GzipDecompressorLibraryFactory,
                 Envoy::Compression::Decompressor::NamedDecompressorLibraryConfigFactory);
} // namespace Decompressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
