#include "utility.h"

#include "envoy/buffer/buffer.h"

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  uint64_t lhs_num_slices = lhs.getRawSlices(nullptr, 0);
  uint64_t rhs_num_slices = rhs.getRawSlices(nullptr, 0);
  if (lhs_num_slices != rhs_num_slices) {
    return false;
  }

  Buffer::RawSlice lhs_slices[lhs_num_slices];
  lhs.getRawSlices(lhs_slices, lhs_num_slices);
  Buffer::RawSlice rhs_slices[rhs_num_slices];
  rhs.getRawSlices(rhs_slices, rhs_num_slices);
  for (size_t i = 0; i < lhs_num_slices; i++) {
    if (lhs_slices[i].len_ != rhs_slices[i].len_) {
      return false;
    }

    if (0 != memcmp(lhs_slices[i].mem_, rhs_slices[i].mem_, lhs_slices[i].len_)) {
      return false;
    }
  }

  return true;
}

std::string TestUtility::bufferToString(const Buffer::Instance& buffer) {
  std::string output;
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  for (uint64_t i = 0; i < num_slices; i++) {
    output.append(static_cast<const char*>(slices[i].mem_), slices[i].len_);
  }

  return output;
}
