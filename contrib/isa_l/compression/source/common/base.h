#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

#include "isa-l.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Common {

/**
 * Shared code between the compressor and the decompressor.
 */
class Base {
public:
  Base(uint64_t chunk_size, std::function<void(isal_zstream*)> zstream_deleter);

  uint32_t checksum();

protected:
  void updateOutput(Buffer::Instance& output_buffer);

  const uint64_t chunk_size_;
  bool initialized_{false};

  const std::unique_ptr<unsigned char[]> chunk_char_ptr_;
  const std::unique_ptr<isal_zstream, std::function<void(isal_zstream*)>> zstream_ptr_;
};

} // namespace Common
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
