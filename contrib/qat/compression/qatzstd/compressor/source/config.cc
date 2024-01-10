#include "contrib/qat/compression/qatzstd/compressor/source/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

QatzstdCompressorFactory::QatzstdCompressorFactory(
    const envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd& qatzstd,
    Event::Dispatcher& dispatcher, Api::Api& api, ThreadLocal::SlotAllocator& tls)
    : compression_level_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(qatzstd, compression_level, ZSTD_CLEVEL_DEFAULT)),
      enable_checksum_(qatzstd.enable_checksum()), strategy_(qatzstd.strategy()),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(qatzstd, chunk_size, ZSTD_CStreamOutSize())) {
  if (qatzstd.has_dictionary()) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::DataSource> dictionaries;
    dictionaries.Add()->CopyFrom(qatzstd.dictionary());
    cdict_manager_ = std::make_unique<ZstdCDictManager>(
        dictionaries, dispatcher, api, tls, true,
        [this](const void* dict_buffer, size_t dict_size) -> ZSTD_CDict* {
          return ZSTD_createCDict(dict_buffer, dict_size, compression_level_);
        });
  }
}

Envoy::Compression::Compressor::CompressorPtr QatzstdCompressorFactory::createCompressor() {
  return std::make_unique<QatzstdCompressorImpl>(compression_level_, enable_checksum_, strategy_,
                                              cdict_manager_, chunk_size_);
}

Envoy::Compression::Compressor::CompressorFactoryPtr
QatzstdCompressorLibraryFactory::createCompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd& proto_config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<QatzstdCompressorFactory>(proto_config, context.mainThreadDispatcher(),
                                                 context.api(), context.threadLocal());
}

/**
 * Static registration for the zstd compressor library. @see NamedCompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(QatzstdCompressorLibraryFactory,
                 Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory);

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
