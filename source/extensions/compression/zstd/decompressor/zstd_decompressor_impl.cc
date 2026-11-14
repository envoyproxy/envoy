#include "source/extensions/compression/zstd/decompressor/zstd_decompressor_impl.h"

#include <limits>

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {

ZstdDecompressorImpl::ZstdDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                                           const ZstdDDictManagerPtr& ddict_manager,
                                           uint32_t chunk_size, uint32_t max_inflate_ratio,
                                           uint32_t max_block_size)
    : Envoy::Compression::Zstd::Common::Base(chunk_size), dctx_(ZSTD_createDCtx(), &ZSTD_freeDCtx),
      ddict_manager_(ddict_manager), stats_(generateStats(stats_prefix, scope)),
      max_inflate_ratio_(max_inflate_ratio), max_block_size_(max_block_size) {}

void ZstdDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                      Buffer::Instance& output_buffer) {
  const uint64_t input_size = input_buffer.length();
  if (input_size == 0 || stopped_on_ratio_limit_) {
    return;
  }
  constexpr uint64_t max_output = std::numeric_limits<uint64_t>::max();
  uint64_t remaining_output = max_inflate_ratio_ > (max_output - max_block_size_) / input_size
                                  ? max_output
                                  : input_size * max_inflate_ratio_ + max_block_size_;

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
      if (!process(output_buffer, remaining_output)) {
        return;
      }
    }
  }
}

bool ZstdDecompressorImpl::process(Buffer::Instance& output_buffer, uint64_t& remaining_output) {
  while (input_.pos < input_.size) {
    const size_t result = ZSTD_decompressStream(dctx_.get(), &output_, &input_);
    if (isError(result)) {
      return false;
    }

    if (output_.pos > remaining_output) {
      stats_.zstd_generic_error_.inc();
      ENVOY_LOG(trace, "excessive decompression ratio detected: {} bytes for {} bytes remaining",
                output_.pos, remaining_output);
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.zstd_decompressor_stop_on_ratio_limit")) {
        // Once a step is discarded, later output would leave a gap in the body.
        stopped_on_ratio_limit_ = true;
      } else {
        getOutput(output_buffer);
      }
      return false;
    }
    remaining_output -= output_.pos;
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
