#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/brotli/decompressor/brotli_decompressor_impl.h"
#include "source/extensions/compression/common/decompressor/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

namespace {
const std::string& brotliStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "brotli."); }
const std::string& brotliExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.brotli.decompressor");
}

} // namespace

class BrotliDecompressorFactory : public Envoy::Compression::Decompressor::DecompressorFactory {
public:
  BrotliDecompressorFactory(
      const envoy::extensions::compression::brotli::decompressor::v3::Brotli& brotli,
      Stats::Scope& scope);

  // Envoy::Compression::Decompressor::DecompressorFactory
  Envoy::Compression::Decompressor::DecompressorPtr
  createDecompressor(const std::string& stats_prefix) override;
  const std::string& statsPrefix() const override { return brotliStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Brotli;
  }

private:
  Stats::Scope& scope_;
  const uint32_t chunk_size_;
  const bool disable_ring_buffer_reallocation_;
};

class BrotliDecompressorLibraryFactory
    : public Compression::Common::Decompressor::DecompressorLibraryFactoryBase<
          envoy::extensions::compression::brotli::decompressor::v3::Brotli> {
public:
  BrotliDecompressorLibraryFactory() : DecompressorLibraryFactoryBase(brotliExtensionName()) {}

private:
  Envoy::Compression::Decompressor::DecompressorFactoryPtr createDecompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::brotli::decompressor::v3::Brotli& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(BrotliDecompressorLibraryFactory);

} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
