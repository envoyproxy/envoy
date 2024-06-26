#pragma once

#include "envoy/compression/compressor/factory.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/extensions/compression/common/compressor/factory_base.h"

#include "contrib/envoy/extensions/compression/qatzstd/compressor/v3alpha/qatzstd.pb.h"
#include "contrib/envoy/extensions/compression/qatzstd/compressor/v3alpha/qatzstd.pb.validate.h"
#include "contrib/qat/compression/qatzstd/compressor/source/qatzstd_compressor_impl.h"
#include "qatseqprod.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

// Default threshold for qat_zstd fallback to software.
const uint32_t DefaultQatZstdFallbackThreshold = 4000;

namespace {

const std::string& qatzstdStatsPrefix() { CONSTRUCT_ON_FIRST_USE(std::string, "qatzstd."); }
const std::string& qatzstdExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.compression.qatzstd.compressor");
}

} // namespace

class QatzstdCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  QatzstdCompressorFactory(
      const envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd& qatzstd,
      ThreadLocal::SlotAllocator& tls);

  // Envoy::Compression::Compressor::CompressorFactory
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override;
  const std::string& statsPrefix() const override { return qatzstdStatsPrefix(); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Zstd;
  }

private:
  struct QatzstdThreadLocal : public ThreadLocal::ThreadLocalObject,
                              public Logger::Loggable<Logger::Id::compression> {
    QatzstdThreadLocal();
    ~QatzstdThreadLocal() override;
    void* GetQATSession();
    bool initialized_;
    void* sequenceProducerState_;
  };
  const uint32_t compression_level_;
  const bool enable_checksum_;
  const uint32_t strategy_;
  const uint32_t chunk_size_;
  const bool enable_qat_zstd_;
  const uint32_t qat_zstd_fallback_threshold_;
  ThreadLocal::TypedSlotPtr<QatzstdThreadLocal> tls_slot_;
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
