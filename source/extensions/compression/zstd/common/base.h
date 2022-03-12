#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

#include "zstd.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Common {

// Keeps a `Zstd` compression stream's state.
struct Base {
  Base(const uint32_t chunk_size);

protected:
  bool isDictionarySet();
  void setInput(const Buffer::RawSlice& input_slice);
  void getOutput(Buffer::Instance& output_buffer);

  std::unique_ptr<uint8_t[]> chunk_ptr_;
  ZSTD_outBuffer output_;
  ZSTD_inBuffer input_;
  unsigned dictionary_id_{0};
  bool is_dictionary_set_{false};
};

} // namespace Common
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
