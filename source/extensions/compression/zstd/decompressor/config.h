#pragma once

#include "envoy/compression/decompressor/config.h"
#include "envoy/extensions/compression/zstd/decompressor/v3/zstd.pb.h"
#include "envoy/extensions/compression/zstd/decompressor/v3/zstd.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/common/decompressor/factory_base.h"
#include "source/extensions/compression/zstd/decompressor/zstd_decompressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {

namespace {
const std::string& zstdStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "zstd."); }
const std::string& zstdExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.zstd.decompressor");
}

} // namespace

class ZstdDecompressorFactory : public Envoy::Compression::Decompressor::DecompressorFactory {
public:
  ZstdDecompressorFactory(const envoy::extensions::compression::zstd::decompressor::v3::Zstd& zstd,
                          Stats::Scope& scope, Event::Dispatcher& dispatcher, Api::Api& api,
                          ThreadLocal::SlotAllocator& tls);

  // Envoy::Compression::Decompressor::DecompressorFactory
  Envoy::Compression::Decompressor::DecompressorPtr
  createDecompressor(const std::string& stats_prefix) override;
  const std::string& statsPrefix() const override { return zstdStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Zstd;
  }

private:
  Stats::Scope& scope_;
  const uint32_t chunk_size_;
  ZstdDDictManagerPtr ddict_manager_{nullptr};
};

class ZstdDecompressorLibraryFactory
    : public Compression::Common::Decompressor::DecompressorLibraryFactoryBase<
          envoy::extensions::compression::zstd::decompressor::v3::Zstd> {
public:
  ZstdDecompressorLibraryFactory() : DecompressorLibraryFactoryBase(zstdExtensionName()) {}

private:
  Envoy::Compression::Decompressor::DecompressorFactoryPtr createDecompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::zstd::decompressor::v3::Zstd& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(ZstdDecompressorLibraryFactory);

} // namespace Decompressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
