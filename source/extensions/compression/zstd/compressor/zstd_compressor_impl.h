#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/common/compression/zstd/common/base.h"
#include "source/common/compression/zstd/compressor/zstd_compressor_impl_base.h"
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
class ZstdCompressorImpl : public Envoy::Compression::Zstd::Compressor::ZstdCompressorImplBase {
public:
  ZstdCompressorImpl(uint32_t compression_level, bool enable_checksum, uint32_t strategy,
                     const ZstdCDictManagerPtr& cdict_manager, uint32_t chunk_size);

private:
  void compressPreprocess(Buffer::Instance& buffer,
                          Envoy::Compression::Compressor::State state) override;

  void compressProcess(const Buffer::Instance& buffer, const Buffer::RawSlice& input_slice,
                       Buffer::Instance& accumulation_buffer) override;

  void compressPostprocess(Buffer::Instance& accumulation_buffer) override;

  const ZstdCDictManagerPtr& cdict_manager_;
};

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
