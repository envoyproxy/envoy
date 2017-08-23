#pragma once

#include "envoy/compressor/compressor.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

class GzipCompressorImpl : public Compressor {

public:
  GzipCompressorImpl();
  ~GzipCompressorImpl();
  uint64_t getTotalIn();
  uint64_t getTotalOut();
  bool init();
  bool finish() override;
  bool compress(Buffer::Instance& in, Buffer::Instance& out) override;

private:
  std::unique_ptr<z_stream> ZlibPtr_{nullptr};
  static const uint chunk_{4096};
  static const uint window_bits_{15 | 16};
  static const uint memory_level_{8};
};

} // namespace zlib
} // namespace envoy