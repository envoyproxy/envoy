#include "common/http/message_impl.h"

#include <cstdint>
#include <string>

#include "common/common/stack_array.h"

namespace Envoy {
namespace Http {

std::string MessageImpl::bodyAsString() const {
  std::string ret;
  if (body_) {
    uint64_t num_slices = body_->getRawSlices(nullptr, 0);
    STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
    body_->getRawSlices(slices.begin(), num_slices);
    for (const Buffer::RawSlice& slice : slices) {
      ret.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
    }
  }
  return ret;
}

} // namespace Http
} // namespace Envoy
