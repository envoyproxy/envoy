#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/common/compression/zstd/common/base.h"
#include "source/common/compression/zstd/common/dictionary_manager.h"
#include "source/common/compression/zstd/compressor/zstd_compressor_impl_base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

using ZstdCDictManager =
    Envoy::Compression::Zstd::Common::DictionaryManager<ZSTD_CDict, ZSTD_freeCDict, ZSTD_getDictID_fromCDict>;
using ZstdCDictManagerPtr = std::unique_ptr<ZstdCDictManager>;

/**
 * Implementation of compressor's interface.
 */
class QatzstdCompressorImpl : public Envoy::Compression::Zstd::Compressor::ZstdCompressorImplBase {
public:
  QatzstdCompressorImpl(uint32_t compression_level, bool enable_checksum, uint32_t strategy,
                     const ZstdCDictManagerPtr& cdict_manager, uint32_t chunk_size):ZstdCompressorImplBase(compression_level,enable_checksum,strategy,cdict_manager,chunk_size){}

private:
  void compressPreprocess() override;

  void compressProcess(const Buffer::RawSlice& input_slice, Buffer::Instance& accumulation_buffer) override;

  void compressPostprocess() override;
};

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
