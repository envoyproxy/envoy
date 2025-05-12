#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Common {

// Keeps a `Brotli` compression stream's state.
struct BrotliContext {
  BrotliContext(uint32_t chunk_size, uint32_t max_output_size = 0);

  void updateOutput(Buffer::Instance& output_buffer);
  void finalizeOutput(Buffer::Instance& output_buffer);

  const uint32_t max_output_size_;
  const uint32_t chunk_size_;
  std::unique_ptr<uint8_t[]> chunk_ptr_;
  const uint8_t* next_in_{};
  uint8_t* next_out_;
  size_t avail_in_{0};
  size_t avail_out_;

private:
  void resetOut();
};

} // namespace Common
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
