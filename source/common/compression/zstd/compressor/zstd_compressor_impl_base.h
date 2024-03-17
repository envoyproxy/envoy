#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/common/compression/zstd/common/base.h"

namespace Envoy {
namespace Compression {
namespace Zstd {
namespace Compressor {

/**
 * Implementation of compressor's interface.
 */
class ZstdCompressorImplBase : public Common::Base,
                               public Envoy::Compression::Compressor::Compressor,
                               NonCopyable {
public:
  ZstdCompressorImplBase(uint32_t compression_level, bool enable_checksum, uint32_t strategy,
                         uint32_t chunk_size);

  // Compression::Compressor::Compressor
  void compress(Buffer::Instance& buffer, Envoy::Compression::Compressor::State state) override;

  void process(Buffer::Instance& output_buffer, ZSTD_EndDirective mode);

protected:
  virtual void compressPreprocess(Buffer::Instance& buffer,
                                  Envoy::Compression::Compressor::State state) PURE;

  virtual void compressProcess(const Buffer::Instance& buffer, const Buffer::RawSlice& input_slice,
                               Buffer::Instance& accumulation_buffer) PURE;

  virtual void compressPostprocess(Buffer::Instance& accumulation_buffer) PURE;

  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> cctx_;
  const uint32_t compression_level_;
};

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Envoy
