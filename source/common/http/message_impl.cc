#include "common/http/message_impl.h"

#include <cstdint>
#include <string>

namespace Envoy {
namespace Http {

std::string MessageImpl::bodyAsString() const {
  std::string ret;
  if (body_) {
    uint64_t num_slices = body_->getRawSlices(nullptr, 0);
    Buffer::RawSlice slices[num_slices];
    body_->getRawSlices(slices, num_slices);
    for (Buffer::RawSlice& slice : slices) {
      ret.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
    }
  }
  return ret;
}

} // namespace Http
} // namespace Envoy
