#pragma once

#include "envoy/compressor/compressor.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

class Impl : public Compressor {

public:
  enum CompressionLevel {
    best = Z_BEST_COMPRESSION,
    normal = Z_DEFAULT_COMPRESSION,
    speed = Z_BEST_SPEED,
    zero = Z_NO_COMPRESSION,
  };

  Impl();
  ~Impl();
  uint64_t getTotalIn();
  uint64_t getTotalOut();
  bool init(CompressionLevel level);
  bool finish() override;
  bool compress(Buffer::Instance& in, Buffer::Instance& out) override;

private:
  z_stream zstream{};
  bool is_finished_{false};
};

} // namespace zlib
} // namespace envoy