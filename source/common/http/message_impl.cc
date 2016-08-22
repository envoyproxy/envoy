#include "message_impl.h"

namespace Http {

std::string MessageImpl::bodyAsString() {
  std::string ret;
  if (body_) {
    uint64_t num_slices = body_->getRawSlices(nullptr, 0);
    Buffer::RawSlice slices[num_slices];
    body_->getRawSlices(slices, num_slices);
    for (uint64_t i = 0; i < num_slices; i++) {
      ret.append(reinterpret_cast<const char*>(slices[i].mem_), slices[i].len_);
    }
  }
  return ret;
}

} // Http
