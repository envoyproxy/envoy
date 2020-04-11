#pragma once

#include "envoy/decompressor/config.h"
#include "envoy/extensions/decompressors/gzip/v3/gzip.pb.h"
#include "envoy/extensions/decompressors/gzip/v3/gzip.pb.validate.h"

#include "common/http/headers.h"

#include "extensions/decompressors/factory_base.h"
#include "extensions/decompressors/gzip/zlib_decompressor.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Decompressors {
namespace Gzip {

namespace {

const std::string& gzipStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "gzip."); }
const std::string& gzipExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.decompressors.gzip");
}

} // namespace

class GzipDecompressorFactory : public Decompressor::DecompressorFactory {
public:
  GzipDecompressorFactory(const envoy::extensions::decompressors::gzip::v3::Gzip& gzip);

  // Decompressor::DecompressorFactory
  Decompressor::DecompressorPtr createDecompressor() override;
  const std::string& statsPrefix() const override { return gzipStatsPrefix(); }
  const std::string& contentEncoding() const override {
    // The decompressor should not need to be aware of HTTP. The decompressor filter should do this
    // work based on a generic hint from the decompressor.
    return Http::Headers::get().ContentEncodingValues.Gzip;
  }

private:
  friend class GzipTest;

  const int32_t window_bits_;
  const uint32_t chunk_size_;
};

class GzipDecompressorLibraryFactory
    : public DecompressorLibraryFactoryBase<envoy::extensions::decompressors::gzip::v3::Gzip> {
public:
  GzipDecompressorLibraryFactory() : DecompressorLibraryFactoryBase(gzipExtensionName()) {}

private:
  Decompressor::DecompressorFactoryPtr createDecompressorLibraryFromProtoTyped(
      const envoy::extensions::decompressors::gzip::v3::Gzip& config) override;
};

DECLARE_FACTORY(GzipDecompressorLibraryFactory);

} // namespace Gzip
} // namespace Decompressors
} // namespace Extensions
} // namespace Envoy
