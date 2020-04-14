#pragma once

#include "envoy/extensions/filters/http/compressor/gzip/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/compressor/gzip/v3/gzip.pb.validate.h"

#include "common/compressor/zlib_compressor_impl.h"
#include "common/http/headers.h"

#include "extensions/filters/http/compressor/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace Gzip {

namespace {

const std::string& gzipStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "gzip."); }
const std::string& gzipExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.http.compressor.gzip");
}

} // namespace

class GzipCompressorFactory : public Compressor::CompressorFactory {
public:
  GzipCompressorFactory(const envoy::extensions::filters::http::compressor::gzip::v3::Gzip& gzip);

  std::unique_ptr<Envoy::Compressor::Compressor> createCompressor() override;
  const std::string& statsPrefix() const override { return gzipStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::Headers::get().ContentEncodingValues.Gzip;
  }

private:
  friend class GzipTest;

  static Envoy::Compressor::ZlibCompressorImpl::CompressionLevel compressionLevelEnum(
      envoy::extensions::filters::http::compressor::gzip::v3::Gzip::CompressionLevel
          compression_level);
  static Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategyEnum(
      envoy::extensions::filters::http::compressor::gzip::v3::Gzip::CompressionStrategy
          compression_strategy);

  Envoy::Compressor::ZlibCompressorImpl::CompressionLevel compression_level_;
  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy compression_strategy_;

  const int32_t memory_level_;
  const int32_t window_bits_;
  const uint32_t chunk_size_;
};

class GzipCompressorLibraryFactory
    : public CompressorLibraryFactoryBase<
          envoy::extensions::filters::http::compressor::gzip::v3::Gzip> {
public:
  GzipCompressorLibraryFactory() : CompressorLibraryFactoryBase(gzipExtensionName()) {}

private:
  Compressor::CompressorFactoryPtr createCompressorLibraryFromProtoTyped(
      const envoy::extensions::filters::http::compressor::gzip::v3::Gzip& config) override;
};

DECLARE_FACTORY(GzipCompressorLibraryFactory);

} // namespace Gzip
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
