#include "source/extensions/compression/gzip/decompressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

namespace {
const uint32_t DefaultWindowBits = 15;
const uint32_t DefaultChunkSize = 4096;
// When logical OR'ed to window bits, this tells zlib library to decompress gzip data per:
// inflateInit2 in https://www.zlib.net/manual.html
const uint32_t GzipHeaderValue = 16;
const uint64_t DefaultMaxInflateRatio = 100;
} // namespace

GzipDecompressorFactory::GzipDecompressorFactory(
    const envoy::extensions::compression::gzip::decompressor::v3::Gzip& gzip, Stats::Scope& scope)
    : scope_(scope),
      window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, window_bits, DefaultWindowBits) |
                   GzipHeaderValue),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, chunk_size, DefaultChunkSize)),
      max_inflate_ratio_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(gzip, max_inflate_ratio, DefaultMaxInflateRatio)) {}

Envoy::Compression::Decompressor::DecompressorPtr
GzipDecompressorFactory::createDecompressor(const std::string& stats_prefix) {
  auto decompressor =
      std::make_unique<ZlibDecompressorImpl>(scope_, stats_prefix, chunk_size_, max_inflate_ratio_);
  decompressor->init(window_bits_);
  return decompressor;
}

Envoy::Compression::Decompressor::DecompressorFactoryPtr
GzipDecompressorLibraryFactory::createDecompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::gzip::decompressor::v3::Gzip& proto_config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<GzipDecompressorFactory>(proto_config, context.scope());
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
