#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/compression/gzip/compressor/v3/gzip.pb.h"
#include "envoy/extensions/compression/gzip/compressor/v3/gzip.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/common/compressor/factory_base.h"
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Compressor {

// Default zlib memory level.
const uint64_t DefaultMemoryLevel = 5;

// Default and maximum compression window size.
const uint64_t DefaultWindowBits = 12;

// When logical OR'ed to window bits, this sets a gzip header and trailer around the compressed
// data.
const uint64_t GzipHeaderValue = 16;

// Default zlib chunk size.
const uint32_t DefaultChunkSize = 4096;

namespace {

const std::string& gzipStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "gzip."); }
const std::string& gzipExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.gzip.compressor");
}

} // namespace

class GzipCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  GzipCompressorFactory(const envoy::extensions::compression::gzip::compressor::v3::Gzip& gzip);

  // Envoy::Compression::Compressor::CompressorFactory
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override;
  const std::string& statsPrefix() const override { return gzipStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Gzip;
  }

private:
  static ZlibCompressorImpl::CompressionLevel
  compressionLevelEnum(envoy::extensions::compression::gzip::compressor::v3::Gzip::CompressionLevel
                           compression_level);
  static ZlibCompressorImpl::CompressionStrategy compressionStrategyEnum(
      envoy::extensions::compression::gzip::compressor::v3::Gzip::CompressionStrategy
          compression_strategy);

  ZlibCompressorImpl::CompressionLevel compression_level_;
  ZlibCompressorImpl::CompressionStrategy compression_strategy_;
  const int32_t memory_level_;
  const int32_t window_bits_;
  const uint32_t chunk_size_;
};

class GzipCompressorLibraryFactory
    : public Compression::Common::Compressor::CompressorLibraryFactoryBase<
          envoy::extensions::compression::gzip::compressor::v3::Gzip> {
public:
  GzipCompressorLibraryFactory() : CompressorLibraryFactoryBase(gzipExtensionName()) {}

private:
  Envoy::Compression::Compressor::CompressorFactoryPtr createCompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::gzip::compressor::v3::Gzip& config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(GzipCompressorLibraryFactory);

} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
