#pragma once

#include <cstdint>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/common/safe_memcpy.h"

namespace Envoy {

/**
 * BitArray is an array of fixed width bits.
 * At a minimum this will allocate a word_size worth of bytes.
 * Methods are deliberately implemented in the header to hint to the compiler to
 * inline.
 *
 * Limitations to this class are:
 *  1) It is not thread-safe
 *  2) It assumes little-endianness and a 64-bit architecture.
 *  3) It works with a maximum width of 32-bits
 */
class BitArray {
public:
  BitArray(int width, size_t num_items)
      : array_start_(std::make_unique<uint8_t[]>(bytesNeeded(width, num_items))),
        end_(array_start_.get() + bytesNeeded(width, num_items)), bit_width_(width),
        mask_((static_cast<uint32_t>(1) << width) - 1), num_items_(num_items) {
    RELEASE_ASSERT(width <= MaxBitWidth, "Using BitArray with invalid parameters.");
    static_assert(
        sizeof(uint8_t*) == 8,
        "Pointer underlying size is not 8 bytes, are we running on a 64-bit architecture?");
  }

  static const int MaxBitWidth = 32;
  static const int WordSize = sizeof(uint8_t*);

  /**
   * Gets the fixed width bit at the given index.
   * Assumes the the provided index is in bounds.
   */
  inline uint32_t get(size_t index) const {
    RELEASE_ASSERT(index < num_items_, "BitArray requested index out of bounds");
    const size_t bit0_offset = index * bit_width_;
    const size_t byte0_offset = bit0_offset >> 3;
    const uint8_t* byte0 = array_start_.get() + byte0_offset;
    const size_t index_of_0th_bit = bit0_offset & 0x7;
    // It is not in the last word, we can memcpy.
    if (byte0 + WordSize <= end_) {
      return (LoadUnsignedWord(byte0) >> index_of_0th_bit) & mask_;
    }
    // We're at the last word, and we've allocated at least a word of memory.
    const uint8_t* last_word = end_ - WordSize;
    return LoadUnsignedWord(last_word) >> (index_of_0th_bit + ((byte0 - last_word) << 3)) & mask_;
  }

  /**
   * Sets the given fixed width bit to the given value.
   */
  inline void set(size_t index, uint32_t value) {
    if (index > num_items_) {
      ENVOY_BUG(true, "BitArray::set requested index out of bounds");
      return;
    }
    const size_t bit0_offset = index * bit_width_;
    const size_t byte0_offset = bit0_offset >> 3;
    uint8_t* byte0 = array_start_.get() + byte0_offset;

    // We are not at the end.
    if (byte0 + WordSize <= end_) {
      const size_t index_of_0th_bit = bit0_offset & 0x7;
      const uint64_t shifted_value = static_cast<uint64_t>(value) << index_of_0th_bit;

      const uint64_t mask_to_write = ((static_cast<uint64_t>(1) << bit_width_) - 1)
                                     << index_of_0th_bit;
      const uint64_t mask_to_preserve = ~mask_to_write;
      const uint64_t value_to_store =
          ((LoadUnsignedWord(byte0) & mask_to_preserve) | (shifted_value & mask_to_write));
      StoreUnsignedWord(byte0, value_to_store);
    } else {
      // We're at the end, so we'll be starting from the last word
      uint8_t* last_word = const_cast<uint8_t*>(end_) - WordSize;
      const size_t index_of_0th_bit = (bit0_offset & 0x7) + ((byte0 - last_word) << 3);
      const uint64_t shifted_value = static_cast<uint64_t>(value) << index_of_0th_bit;
      const uint64_t mask_to_write = ((static_cast<uint64_t>(1) << bit_width_) - 1)
                                     << index_of_0th_bit;
      const uint64_t mask_to_preserve = ~mask_to_write;
      const uint64_t value_to_store =
          ((LoadUnsignedWord(last_word) & mask_to_preserve) | (shifted_value & mask_to_write));
      return StoreUnsignedWord(last_word, value_to_store);
    }
  }

  size_t size() const { return num_items_; }

private:
  static inline size_t bytesNeeded(int bit_width, size_t num_items) {
    // Round up number of bytes needed.
    // To simplify the implementation when accessing the last elements,
    // we must allocate a minimum of a word.
    return std::max((bit_width * num_items + 7) >> 3, static_cast<size_t>(WordSize));
  }

  static inline void StoreUnsignedWord(void* destination, uint64_t value) {
    safeMemcpyUnsafeDst(destination, &value);
  }

  static inline uint64_t LoadUnsignedWord(const void* source) {
    uint64_t destination;
    safeMemcpyUnsafeSrc(&destination, source);
    return destination;
  }

  // Backing storage for the underlying array of bits.
  std::unique_ptr<uint8_t[]> array_start_;
  // Pointer to the end of the array. In cases where we allocate a word size of
  // bytes it's possible that the logical "end" of the e.g. based on num_items
  // is before this address.
  const uint8_t* end_;
  // The fixed bit width of the elements in the array.
  const int bit_width_;
  const uint32_t mask_;
  const size_t num_items_;
};

} // namespace Envoy
