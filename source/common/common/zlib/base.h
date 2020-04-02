#pragma once

#include "common/common/logger.h"
#include "envoy/buffer/buffer.h"

#include <memory>

#include "zlib.h"

namespace Envoy {
namespace Zlib {

/**
 * Shared code between the compressor and the decompressor.
 */
class Base {
public:
  Base(uint64_t chunk_size, std::function<void(z_stream*)> zstream_deleter);

  /**
   * It returns the checksum of all output produced so far. Decompressor's checksum at the end of
   * the stream has to match compressor's checksum produced at the end of the compression.
   * @return uint64_t CRC-32 if a gzip stream is being read or Adler-32 for other compression
   * types.
   */
  uint64_t checksum();

protected:
  void updateOutput(Buffer::Instance& output_buffer);

  const uint64_t chunk_size_;
  bool initialized_{false};

  std::unique_ptr<unsigned char[]> chunk_char_ptr_;
  std::unique_ptr<z_stream, std::function<void(z_stream*)>> zstream_ptr_;
};

} // namespace Zlib
} // namespace Envoy
