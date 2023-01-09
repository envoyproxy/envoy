#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/compression/brotli/compressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/brotli/compressor/v3/brotli.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/brotli/compressor/brotli_compressor_impl.h"
#include "source/extensions/compression/common/compressor/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Compressor {

// Default input block size.
const uint32_t DefaultInputBlockBits = 24;

// Default compression window size.
const uint32_t DefaultWindowBits = 18;

// Default quality.
const uint32_t DefaultQuality = 3;

// Default zlib chunk size.
const uint32_t DefaultChunkSize = 4096;

namespace {

const std::string& brotliStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "brotli."); }
const std::string& brotliExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.brotli.compressor");
}

} // namespace

class BrotliCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  BrotliCompressorFactory(
      const envoy::extensions::compression::brotli::compressor::v3::Brotli& brotli);

  // Envoy::Compression::Compressor::CompressorFactory
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override;
  const std::string& statsPrefix() const override { return brotliStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Brotli;
  }

private:
  static BrotliCompressorImpl::EncoderMode encoderModeEnum(
      envoy::extensions::compression::brotli::compressor::v3::Brotli::EncoderMode encoder_mode);
  const uint32_t chunk_size_;
  const bool disable_literal_context_modeling_;
  const BrotliCompressorImpl::EncoderMode encoder_mode_;
  const uint32_t input_block_bits_;
  const uint32_t quality_;
  const uint32_t window_bits_;
};

class BrotliCompressorLibraryFactory
    : public Compression::Common::Compressor::CompressorLibraryFactoryBase<
          envoy::extensions::compression::brotli::compressor::v3::Brotli> {
public:
  BrotliCompressorLibraryFactory() : CompressorLibraryFactoryBase(brotliExtensionName()) {}

private:
  Envoy::Compression::Compressor::CompressorFactoryPtr createCompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::brotli::compressor::v3::Brotli& config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(BrotliCompressorLibraryFactory);

} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
