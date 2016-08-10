#include "message_impl.h"

namespace Http {

std::string MessageImpl::bodyAsString() {
  std::string ret;
  if (body_) {
    for (Buffer::RawSlice& slice : body_->getRawSlices()) {
      ret.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
    }
  }
  return ret;
}

} // Http
