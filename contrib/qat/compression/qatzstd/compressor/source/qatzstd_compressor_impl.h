#pragma once

#include "envoy/compression/compressor/compressor.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/compression/zstd/common/base.h"
#include "source/common/compression/zstd/compressor/zstd_compressor_impl_base.h"

#include "qatseqprod.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {

/**
 * Implementation of compressor's interface.
 */
class QatzstdCompressorImpl : public Envoy::Compression::Zstd::Compressor::ZstdCompressorImplBase,
                              public Logger::Loggable<Logger::Id::compression> {
public:
  QatzstdCompressorImpl(uint32_t compression_level, bool enable_checksum, uint32_t strategy,
                        uint32_t chunk_size, bool enable_qat_zstd,
                        uint32_t qat_zstd_fallback_threshold, void* sequenceProducerState);

private:
  void compressPreprocess(Buffer::Instance& buffer,
                          Envoy::Compression::Compressor::State state) override;

  void compressProcess(const Buffer::Instance& buffer, const Buffer::RawSlice& slice,
                       Buffer::Instance& accumulation_buffer) override;

  void compressPostprocess(Buffer::Instance& accumulation_buffer) override;

  void setInput(const uint8_t* input, size_t size);

  bool enable_qat_zstd_;
  const uint32_t qat_zstd_fallback_threshold_;
  void* sequenceProducerState_;
  std::unique_ptr<uint8_t[]> input_ptr_;
  uint64_t input_len_;
  uint64_t chunk_size_;
};

} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
