#pragma once

#include <cstdint>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/common/safe_memcpy.h"

namespace Envoy {

#define ENVOY_BIT_ARRAY_SUPPORTED sizeof(uint8_t*) == 8

/**
 * BitArray is an fixed sized array of fixed bit width elements. As such once
 * constructed it will not grow in the number of elements it can hold, or change
 * the number of bits an element is considered.
 *
 * Note that a BitArray is different from std::bitset as std::bitset elements
 * can only have a bit width of 1, while BitArray elements have a fixed bit width ranging from 1 to
 * 32 bits. Moreover BitArray is significantly more compact than std::bitset if using bitset to
 * represent integers.
 *
 * At a minimum this will allocate 2 * word_size worth of bytes. To simplify the
 * implementation for accessing data in the last word, we allocate an additional
 * word_size of bytes beyond what is necessary to hold the items in the BitArray.
 *
 * Methods are deliberately implemented in the header to hint to the compiler to
 * inline.
 *
 * Limitations to this class are:
 *  1) It is thread compatible.
 *  2) It assumes a 64-bit architecture.
 *  3) It works with a maximum width of 32-bits
 */
class BitArray {
public:
  /**
   * Constructs a BitArray.
   * @param width the fixed width of the array elements. For example, if we need
   *  to represent 10 distinct values we need at least 4 bits as the width.
   * @param num_items the number of elements the bit array must hold.
   */
  BitArray(int width, size_t num_items)
      : array_start_(std::make_unique<uint8_t[]>(bytesNeeded(width, num_items))), bit_width_(width),
        // This will fit in a uint32_t as with the maximum shift of 32, we'd
        // subtract one to fit in 32 bits.
        mask_(static_cast<uint32_t>((static_cast<uint64_t>(1) << width) - 1)),
        num_items_(num_items) {
    RELEASE_ASSERT(width <= MaxBitWidth, "Using BitArray with invalid parameters.");
    RELEASE_ASSERT(ENVOY_BIT_ARRAY_SUPPORTED, "BitArray requires 64-bit architecture.");
    // Init padding to avoid sanitizer complaints if reading the last elements.
    uint8_t* padding_start = array_start_.get() + (bytesNeeded(width, num_items) - WordSize);
    storeUnsignedWord(padding_start, 0);
  }

  static constexpr int MaxBitWidth = 32;
  static constexpr int WordSize = sizeof(uint8_t*);

  /**
   * Gets the fixed width bit at the given index.
   * Assumes the the provided index is in bounds.
   */
  inline uint32_t get(size_t index) const {
    RELEASE_ASSERT(index < num_items_, "BitArray requested index out of bounds");
    // We locate the first byte that contains part of the element located at
    // the given index.
    const size_t bit0_offset = index * bit_width_;
    const size_t byte0_offset = bit0_offset >> 3;
    const uint8_t* byte0 = array_start_.get() + byte0_offset;
    // Find the starting bit within byte0 of this element.
    // It will be in the range of 0-7.
    const size_t index_of_0th_bit = bit0_offset & 0x7;
    // We then load regardless of alignment an entire word to get the bytes
    // containing the element. We shift this to get have the element's bits
    // at bit 0, and then apply the mask which will capture all the bits
    // pertaining to this element.
    return (loadUnsignedWord(byte0) >> index_of_0th_bit) & mask_;
  }

  /**
   * Sets the given fixed width bit to the given value.
   */
  inline void set(size_t index, uint32_t value) {
    if (index > num_items_) {
      ENVOY_BUG(true, "BitArray::set requested index out of bounds");
      return;
    }
    // We locate the first byte that contains part of the element located at
    // the given index.
    const size_t bit0_offset = index * bit_width_;
    const size_t byte0_offset = bit0_offset >> 3;
    uint8_t* byte0 = array_start_.get() + byte0_offset;

    // Find the starting bit within byte0 of this element.
    // It will be in the range of 0-7.
    const size_t index_of_0th_bit = bit0_offset & 0x7;
    // We shift the value we're assigning to the element to align with the
    // start of the element in the word we will load.
    const uint64_t shifted_value = static_cast<uint64_t>(value) << index_of_0th_bit;

    // Create masks the we'll use to capture the bits to write and preserve the
    // bits in the word we won't write.
    const uint64_t mask_to_write = ((static_cast<uint64_t>(1) << bit_width_) - 1)
                                   << index_of_0th_bit;
    const uint64_t mask_to_preserve = ~mask_to_write;

    // We bitwise-and the loaded bytes with the mask to preserve to both
    // preserve the elements that aren't this entry and zero out the bits.
    // We then bitwise-or this to capture the value we're assigning to the
    // element.
    const uint64_t value_to_store =
        ((loadUnsignedWord(byte0) & mask_to_preserve) | (shifted_value & mask_to_write));
    storeUnsignedWord(byte0, value_to_store);
  }

  size_t size() const { return num_items_; }

private:
  static inline size_t bytesNeeded(int bit_width, size_t num_items) {
    // Round up number of bytes needed.
    const size_t bytes_required = (bit_width * num_items + 7) >> 3;
    // We add padding to simplify implementation.
    const size_t padding = static_cast<size_t>(WordSize);

    // To simplify the implementation we must allocate a minimum of a word to
    // hold the elements.
    const size_t minimum_bytes_required_without_padding = static_cast<size_t>(WordSize);
    return std::max(bytes_required, minimum_bytes_required_without_padding) + padding;
  }

  static inline void storeUnsignedWord(void* destination, uint64_t value) {
    value = htole64(value);
    safeMemcpyUnsafeDst(destination, &value);
  }

  static inline uint64_t loadUnsignedWord(const void* source) {
    uint64_t destination;
    safeMemcpyUnsafeSrc(&destination, source);
    return le64toh(destination);
  }

  // Backing storage for the underlying array of bits.
  std::unique_ptr<uint8_t[]> array_start_;
  // Pointer to the end of the array. In cases where we allocate a word size of
  // bytes it's possible that the logical "end" of the e.g. based on num_items
  // is before this address.
  // The fixed bit width of the elements in the array.
  const int bit_width_;
  const uint32_t mask_;
  const size_t num_items_;
};

} // namespace Envoy
