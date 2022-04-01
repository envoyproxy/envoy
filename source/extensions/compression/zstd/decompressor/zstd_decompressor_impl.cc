#include "source/extensions/compression/zstd/decompressor/zstd_decompressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {

ZstdDecompressorImpl::ZstdDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                                           const ZstdDDictManagerPtr& ddict_manager,
                                           uint32_t chunk_size)
    : Common::Base(chunk_size), dctx_(ZSTD_createDCtx(), &ZSTD_freeDCtx),
      ddict_manager_(ddict_manager), stats_(generateStats(stats_prefix, scope)) {}

void ZstdDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                      Buffer::Instance& output_buffer) {
  for (const Buffer::RawSlice& input_slice : input_buffer.getRawSlices()) {
    if (input_slice.len_ > 0) {
      if (ddict_manager_ && !is_dictionary_set_) {
        is_dictionary_set_ = true;
        // If id == 0, it means that dictionary id could not be decoded.
        dictionary_id_ =
            ZSTD_getDictID_fromFrame(static_cast<uint8_t*>(input_slice.mem_), input_slice.len_);
        if (dictionary_id_ != 0) {
          auto dictionary = ddict_manager_->getDictionaryById(dictionary_id_);
          if (!dictionary) {
            stats_.zstd_dictionary_error_.inc();
            return;
          }
          const size_t result = ZSTD_DCtx_refDDict(dctx_.get(), dictionary);
          if (isError(result)) {
            return;
          }
        }
      }

      setInput(input_slice);
      if (!process(output_buffer)) {
        return;
      }
    }
  }
}

bool ZstdDecompressorImpl::process(Buffer::Instance& output_buffer) {
  while (input_.pos < input_.size) {
    const size_t result = ZSTD_decompressStream(dctx_.get(), &output_, &input_);
    if (isError(result)) {
      return false;
    }

    getOutput(output_buffer);
  }

  return true;
}

bool ZstdDecompressorImpl::isError(size_t result) {
  switch (ZSTD_getErrorCode(result)) {
  case ZSTD_error_no_error:
    return false;
  case ZSTD_error_memory_allocation:
    stats_.zstd_memory_error_.inc();
    break;
  case ZSTD_error_dictionary_corrupted:
  case ZSTD_error_dictionary_wrong:
    stats_.zstd_dictionary_error_.inc();
    break;
  case ZSTD_error_checksum_wrong:
    stats_.zstd_checksum_wrong_error_.inc();
    break;
  default:
    stats_.zstd_generic_error_.inc();
    break;
  }
  return true;
}

} // namespace Decompressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
