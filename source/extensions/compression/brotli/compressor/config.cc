#include "extensions/compression/brotli/compressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Compressor {

namespace {
// Default input block size.
const uint32_t DefaultInputBlockBits = 24;

// Default compression window size.
const uint32_t DefaultWindowBits = 22;

// Default quality.
// Since protobuf numeric objects default to zero values if not present in a
// message and zero is a legitimate value for quality in the brotli library
// we cannot have default quality other than zero without additional tweaks.
// So instead of the 0-11 range we use 1-12 in the config and decrement the actual
// value passed to the compressor by one.
const uint32_t DefaultQuality = 12;

// Default zlib chunk size.
const uint32_t DefaultChunkSize = 4096;
} // namespace

BrotliCompressorFactory::BrotliCompressorFactory(
    const envoy::extensions::compression::brotli::compressor::v3::Brotli& brotli)
    : chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(brotli, chunk_size, DefaultChunkSize)),
      disable_literal_context_modeling_(brotli.disable_literal_context_modeling()),
      encoder_mode_(encoderModeEnum(brotli.encoder_mode())),
      input_block_bits_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(brotli, input_block_bits, DefaultInputBlockBits)),
      quality_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(brotli, quality, DefaultQuality) - 1),
      window_bits_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(brotli, window_bits, DefaultWindowBits)) {}

Envoy::Compression::Compressor::CompressorPtr BrotliCompressorFactory::createCompressor() {
  return std::make_unique<BrotliCompressorImpl>(quality_, window_bits_, input_block_bits_,
                                                disable_literal_context_modeling_, encoder_mode_,
                                                chunk_size_);
}

BrotliCompressorImpl::EncoderMode BrotliCompressorFactory::encoderModeEnum(
    envoy::extensions::compression::brotli::compressor::v3::Brotli::EncoderMode encoder_mode) {
  switch (encoder_mode) {
  case envoy::extensions::compression::brotli::compressor::v3::Brotli::GENERIC:
    return BrotliCompressorImpl::EncoderMode::Generic;
  case envoy::extensions::compression::brotli::compressor::v3::Brotli::TEXT:
    return BrotliCompressorImpl::EncoderMode::Text;
  case envoy::extensions::compression::brotli::compressor::v3::Brotli::FONT:
    return BrotliCompressorImpl::EncoderMode::Font;
  default:
    return BrotliCompressorImpl::EncoderMode::Default;
  }
}

Envoy::Compression::Compressor::CompressorFactoryPtr
BrotliCompressorLibraryFactory::createCompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::brotli::compressor::v3::Brotli& proto_config) {
  return std::make_unique<BrotliCompressorFactory>(proto_config);
}

/**
 * Static registration for the brotli compressor library. @see NamedCompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(BrotliCompressorLibraryFactory,
                 Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory);

} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
