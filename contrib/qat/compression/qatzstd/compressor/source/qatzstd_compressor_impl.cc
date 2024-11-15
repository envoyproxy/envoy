#include "contrib/qat/compression/qatzstd/compressor/source/qatzstd_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

QatzstdCompressorImpl::QatzstdCompressorImpl(uint32_t compression_level, bool enable_checksum,
                                             uint32_t strategy, uint32_t chunk_size,
                                             bool enable_qat_zstd,
                                             uint32_t qat_zstd_fallback_threshold,
                                             void* sequenceProducerState)
    : ZstdCompressorImplBase(compression_level, enable_checksum, strategy, chunk_size),
      enable_qat_zstd_(enable_qat_zstd), qat_zstd_fallback_threshold_(qat_zstd_fallback_threshold),
      sequenceProducerState_(sequenceProducerState), input_ptr_{std::make_unique<uint8_t[]>(
                                                         chunk_size)},
      input_len_(0), chunk_size_(chunk_size) {
  size_t result;
  result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_compressionLevel, compression_level_);
  RELEASE_ASSERT(!ZSTD_isError(result), "");

  ENVOY_LOG(debug,
            "zstd new ZstdCompressorImpl, compression_level: {}, strategy: {}, chunk_size: "
            "{}, enable_qat_zstd: {}, qat_zstd_fallback_threshold: {}",
            compression_level, strategy, chunk_size, enable_qat_zstd, qat_zstd_fallback_threshold);
  if (enable_qat_zstd_) {
    /* register qatSequenceProducer */
    ZSTD_registerSequenceProducer(cctx_.get(), sequenceProducerState_, qatSequenceProducer);
    result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_enableSeqProducerFallback, 1);
    RELEASE_ASSERT(!ZSTD_isError(result), "");
  }
}

void QatzstdCompressorImpl::compressPreprocess(Buffer::Instance& buffer,
                                               Envoy::Compression::Compressor::State state) {
  ENVOY_LOG(debug, "zstd compress input size {}", buffer.length());
  if (enable_qat_zstd_ && state == Envoy::Compression::Compressor::State::Flush) {
    // Fall back to software if input size less than threshold to achieve better performance.
    if (buffer.length() < qat_zstd_fallback_threshold_) {
      ENVOY_LOG(debug, "zstd compress fall back to software");
      ZSTD_registerSequenceProducer(cctx_.get(), nullptr, nullptr);
    }
  }
}

void QatzstdCompressorImpl::setInput(const uint8_t* input, size_t size) {
  input_.src = input;
  input_.pos = 0;
  input_.size = size;
  input_len_ = 0;
}

void QatzstdCompressorImpl::compressProcess(const Buffer::Instance& buffer,
                                            const Buffer::RawSlice& slice,
                                            Buffer::Instance& accumulation_buffer) {
  if (slice.len_ == buffer.length() || slice.len_ > chunk_size_) {
    if (input_len_ > 0) {
      setInput(input_ptr_.get(), input_len_);
      process(accumulation_buffer, ZSTD_e_continue);
    }
    setInput(static_cast<uint8_t*>(slice.mem_), slice.len_);
    process(accumulation_buffer, ZSTD_e_continue);
  } else {
    if (input_len_ + slice.len_ > chunk_size_) {
      setInput(input_ptr_.get(), input_len_);
      process(accumulation_buffer, ZSTD_e_continue);
    }
    memcpy(input_ptr_.get() + input_len_, slice.mem_, slice.len_); // NOLINT(safe-memcpy)
    input_len_ += slice.len_;
  }
}

void QatzstdCompressorImpl::compressPostprocess(Buffer::Instance& accumulation_buffer) {
  if (input_len_ > 0) {
    setInput(input_ptr_.get(), input_len_);
    process(accumulation_buffer, ZSTD_e_continue);
  }
}

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
