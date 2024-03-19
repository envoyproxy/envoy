#include "source/extensions/compression/zstd/compressor/zstd_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Compressor {

ZstdCompressorImpl::ZstdCompressorImpl(uint32_t compression_level, bool enable_checksum,
                                       uint32_t strategy, const ZstdCDictManagerPtr& cdict_manager,
                                       uint32_t chunk_size)
    : ZstdCompressorImplBase(compression_level, enable_checksum, strategy, chunk_size),
      cdict_manager_(cdict_manager) {
  size_t result;
  if (cdict_manager_) {
    ZSTD_CDict* cdict = cdict_manager_->getFirstDictionary();
    result = ZSTD_CCtx_refCDict(cctx_.get(), cdict);
  } else {
    result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_compressionLevel, compression_level_);
  }
  RELEASE_ASSERT(!ZSTD_isError(result), "");
}

void ZstdCompressorImpl::compressPreprocess(Buffer::Instance&,
                                            Envoy::Compression::Compressor::State) {}

void ZstdCompressorImpl::compressProcess(const Buffer::Instance&,
                                         const Buffer::RawSlice& input_slice,
                                         Buffer::Instance& accumulation_buffer) {
  setInput(input_slice);
  process(accumulation_buffer, ZSTD_e_continue);
}

void ZstdCompressorImpl::compressPostprocess(Buffer::Instance&) {}

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
