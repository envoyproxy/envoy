#include "source/extensions/compression/zstd/compressor/zstd_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Compressor {

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
