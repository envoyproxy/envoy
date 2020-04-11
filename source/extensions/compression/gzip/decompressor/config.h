#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.validate.h"

#include "common/http/headers.h"

#include "extensions/compression/common/decompressor/factory_base.h"
#include "extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

namespace {

const std::string& gzipStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "gzip."); }
const std::string& gzipExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.decompressors.gzip");
}

} // namespace

class GzipDecompressorFactory : public Envoy::Compression::Decompressor::DecompressorFactory {
public:
  GzipDecompressorFactory(const envoy::extensions::compression::gzip::decompressor::v3::Gzip& gzip);

  // Envoy::Compression::Decompressor::DecompressorFactory
  Envoy::Compression::Decompressor::DecompressorPtr createDecompressor() override;
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
    : public Common::Decompressor::DecompressorLibraryFactoryBase<
          envoy::extensions::compression::gzip::decompressor::v3::Gzip> {
public:
  GzipDecompressorLibraryFactory() : DecompressorLibraryFactoryBase(gzipExtensionName()) {}

private:
  Envoy::Compression::Decompressor::DecompressorFactoryPtr createDecompressorLibraryFromProtoTyped(
      const envoy::extensions::compression::gzip::decompressor::v3::Gzip& config) override;
};

DECLARE_FACTORY(GzipDecompressorLibraryFactory);

} // namespace Decompressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
