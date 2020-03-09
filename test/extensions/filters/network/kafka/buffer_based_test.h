#pragma once

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/kafka/serialization.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// Common utilities for various Kafka buffer-related tests.

/**
 * Utility superclass that keeps a buffer that can be played with during the test.
 */
class BufferBasedTest {
protected:
  const char* getBytes() {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    absl::FixedArray<Buffer::RawSlice> slices(num_slices);
    buffer_.getRawSlices(slices.begin(), num_slices);
    return reinterpret_cast<const char*>((slices[0]).mem_);
  }

  template <typename T> uint32_t putIntoBuffer(const T& arg) {
    EncodingContext encoder_{-1}; // Context's api_version is not used when serializing primitives.
    return encoder_.encode(arg, buffer_);
  }

  absl::string_view putGarbageIntoBuffer(uint32_t size = 1024) {
    putIntoBuffer(Bytes(size));
    return {getBytes(), size};
  }

  Buffer::OwnedImpl buffer_;
};

/**
 * Utility superclass that keeps a buffer and can put messages into buffer.
 * @param Encoder class used for encoding messages into buffer
 */
template <class Encoder> class MessageBasedTest : public BufferBasedTest {
protected:
  template <typename T> void putMessageIntoBuffer(const T& arg) {
    Encoder encoder{buffer_};
    encoder.encode(arg);
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
