#pragma once

#include "envoy/compression/compressor/factory.h"
#include "contrib/envoy/extensions/compression/compressor/igzip/v3alpha/igzip.pb.h"
#include "contrib/envoy/extensions/compression/compressor/igzip/v3alpha/igzip.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/common/compressor/factory_base.h"
#include "contrib/isa_l/compression/source/compressor/igzip_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Compressor {

namespace {

const std::string& igzipStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "igzip."); }
const std::string& igzipExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.compressor.igzip");
}

} // namespace

class IgzipCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  IgzipCompressorFactory(const envoy::extensions::compression::compressor::igzip::v3alpha::Igzip& igzip);

  // Envoy::Compression::Compressor::CompressorFactory
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override;
  const std::string& statsPrefix() const override { return igzipStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Gzip;
  }

private:
  static IgzipCompressorImpl::CompressionLevel
  compressionLevelEnum(envoy::extensions::compression::compressor::igzip::v3alpha::Igzip::CompressionLevel
                           compression_level);

  IgzipCompressorImpl::CompressionLevel compression_level_;
  // const int32_t memory_level_;
  const int32_t window_bits_;
  const uint32_t chunk_size_;
};

class IgzipCompressorLibraryFactory
    : public Compression::Common::Compressor::CompressorLibraryFactoryBase<
          envoy::extensions::compression::compressor::igzip::v3alpha::Igzip> {
public:
  IgzipCompressorLibraryFactory() : CompressorLibraryFactoryBase(igzipExtensionName()) {}

private:
  Envoy::Compression::Compressor::CompressorFactoryPtr createCompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::compressor::igzip::v3alpha::Igzip& config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(IgzipCompressorLibraryFactory);

} // namespace Compressor
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
