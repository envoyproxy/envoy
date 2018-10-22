#include "common/http/message_impl.h"

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"

namespace Envoy {
namespace Http {

std::string MessageImpl::bodyAsString() const {
  std::string ret;
  if (body_) {
    uint64_t num_slices = body_->getRawSlices(nullptr, 0);
    STACK_ALLOC_ARRAY(slices, Buffer::RawSlice, num_slices);
    body_->getRawSlices(slices, num_slices);
    for (uint64_t i = 0; i < num_slices; i++) {
      Buffer::RawSlice& slice = slices[i];
      ret.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
    }
  }
  return ret;
}

} // namespace Http
} // namespace Envoy
