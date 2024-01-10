#pragma once

#include "envoy/compression/compressor/factory.h"
#include "contrib/envoy/extensions/compression/qatzstd/compressor/v3alpha/qatzstd.pb.h"
#include "contrib/envoy/extensions/compression/qatzstd/compressor/v3alpha/qatzstd.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/compression/common/compressor/factory_base.h"
#include "contrib/qat/compression/qatzstd/compressor/source/qatzstd_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

namespace {

const std::string& qatzstdStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "qatzstd."); }
const std::string& qatzstdExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.qatzstd.compressor");
}

} // namespace

class QatzstdCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  QatzstdCompressorFactory(const envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd& qatzstd,
                        Event::Dispatcher& dispatcher, Api::Api& api,
                        ThreadLocal::SlotAllocator& tls);

  // Envoy::Compression::Compressor::CompressorFactory
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override;
  const std::string& statsPrefix() const override { return qatzstdStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Zstd;
  }

private:
  const uint32_t compression_level_;
  const bool enable_checksum_;
  const uint32_t strategy_;
  const uint32_t chunk_size_;
  ZstdCDictManagerPtr cdict_manager_{nullptr};
};

class QatzstdCompressorLibraryFactory
    : public Compression::Common::Compressor::CompressorLibraryFactoryBase<
          envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd> {
public:
  QatzstdCompressorLibraryFactory() : CompressorLibraryFactoryBase(qatzstdExtensionName()) {}

private:
  Envoy::Compression::Compressor::CompressorFactoryPtr createCompressorFactoryFromProtoTyped(
      const envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd& config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(QatzstdCompressorLibraryFactory);

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
