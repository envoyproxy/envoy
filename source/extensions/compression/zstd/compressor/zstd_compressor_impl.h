#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/extensions/compression/zstd/common/base.h"
#include "source/extensions/compression/zstd/common/dictionary_manager.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Compressor {

using ZstdCDictManager =
    Common::DictionaryManager<ZSTD_CDict, ZSTD_freeCDict, ZSTD_getDictID_fromCDict>;
using ZstdCDictManagerPtr = std::unique_ptr<ZstdCDictManager>;

/**
 * Implementation of compressor's interface.
 */
class ZstdCompressorImpl : public Common::Base,
                           public Envoy::Compression::Compressor::Compressor,
                           NonCopyable {
public:
  ZstdCompressorImpl(uint32_t compression_level, bool enable_checksum, uint32_t strategy,
                     const ZstdCDictManagerPtr& cdict_manager, uint32_t chunk_size);

  // Compression::Compressor::Compressor
  void compress(Buffer::Instance& buffer, Envoy::Compression::Compressor::State state) override;

private:
  void process(Buffer::Instance& output_buffer, ZSTD_EndDirective mode);

  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> cctx_;
  const ZstdCDictManagerPtr& cdict_manager_;
  const uint32_t compression_level_;
};

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
