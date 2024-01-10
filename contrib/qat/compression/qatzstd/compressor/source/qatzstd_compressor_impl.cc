#include "contrib/qat/compression/qatzstd/compressor/source/qatzstd_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

void QatzstdCompressorImpl::compressPreprocess() {}

void QatzstdCompressorImpl::compressProcess(const Buffer::RawSlice& input_slice, Buffer::Instance& accumulation_buffer) {
  setInput(input_slice);
  process(accumulation_buffer, ZSTD_e_continue);
}

void QatzstdCompressorImpl::compressPostprocess() {}

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
