#include "source/extensions/compression/zstd/decompressor/config.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {

ZstdDecompressorFactory::ZstdDecompressorFactory(
    const envoy::extensions::compression::zstd::decompressor::v3::Zstd& zstd, Stats::Scope& scope,
    Event::Dispatcher& dispatcher, Api::Api& api, ThreadLocal::SlotAllocator& tls)
    : scope_(scope),
      chunk_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(zstd, chunk_size, ZSTD_DStreamOutSize())) {
  if (zstd.dictionaries_size() > 0) {
    ddict_manager_ = std::make_unique<ZstdDDictManager>(
        zstd.dictionaries(), dispatcher, api, tls, false,
        [](const void* dict_buffer, size_t dict_size) -> ZSTD_DDict* {
          return ZSTD_createDDict(dict_buffer, dict_size);
        });
  }
}

Envoy::Compression::Decompressor::DecompressorPtr
ZstdDecompressorFactory::createDecompressor(const std::string& stats_prefix) {
  return std::make_unique<ZstdDecompressorImpl>(scope_, stats_prefix, ddict_manager_, chunk_size_);
}

Envoy::Compression::Decompressor::DecompressorFactoryPtr
ZstdDecompressorLibraryFactory::createDecompressorFactoryFromProtoTyped(
    const envoy::extensions::compression::zstd::decompressor::v3::Zstd& proto_config,
    Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();
  return std::make_unique<ZstdDecompressorFactory>(
      proto_config, context.scope(), server_context.mainThreadDispatcher(), server_context.api(),
      server_context.threadLocal());
}

/**
 * Static registration for the zstd decompressor. @see NamedDecompressorLibraryConfigFactory.
 */
REGISTER_FACTORY(ZstdDecompressorLibraryFactory,
                 Envoy::Compression::Decompressor::NamedDecompressorLibraryConfigFactory);
} // namespace Decompressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
