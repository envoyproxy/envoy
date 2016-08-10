#include "utility.h"

#include "envoy/buffer/buffer.h"

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  std::vector<Buffer::RawSlice> lhs_slices = lhs.getRawSlices();
  std::vector<Buffer::RawSlice> rhs_slices = rhs.getRawSlices();
  if (lhs_slices.size() != rhs_slices.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs_slices.size(); i++) {
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
  for (Buffer::RawSlice& slice : buffer.getRawSlices()) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}
