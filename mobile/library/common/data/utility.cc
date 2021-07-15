#include "library/common/data/utility.h"

#include <stdlib.h>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"

#include "library/common/buffer/bridge_fragment.h"

namespace Envoy {
namespace Data {
namespace Utility {

void updateMaxBytes(uint32_t& max_bytes, const Buffer::Instance& data) {
  if (max_bytes == 0) {
    max_bytes = data.length();
  } else {
    max_bytes = std::min<uint32_t>(max_bytes, data.length());
  }
}

Buffer::InstancePtr toInternalData(envoy_data data) {
  // This fragment only needs to live until done is called.
  // Therefore, it is sufficient to allocate on the heap, and delete in the done method.
  Buffer::BridgeFragment* fragment = Buffer::BridgeFragment::createBridgeFragment(data);
  Buffer::InstancePtr buf = std::make_unique<Buffer::OwnedImpl>();
  buf->addBufferFragment(*fragment);
  return buf;
}

envoy_data toBridgeData(Buffer::Instance& data, uint32_t max_bytes) {
  updateMaxBytes(max_bytes, data);
  envoy_data bridge_data = copyToBridgeData(data, max_bytes);
  data.drain(bridge_data.length);
  return bridge_data;
}

envoy_data copyToBridgeData(absl::string_view str) {
  uint8_t* buffer = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * str.length()));
  memcpy(buffer, str.data(), str.length()); // NOLINT(safe-memcpy)
  return {str.length(), buffer, free, buffer};
}

envoy_data copyToBridgeData(const Buffer::Instance& data, uint32_t max_bytes) {
  updateMaxBytes(max_bytes, data);
  uint8_t* buffer = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * max_bytes));
  data.copyOut(0, max_bytes, buffer);
  return {static_cast<size_t>(max_bytes), buffer, free, buffer};
}

std::string copyToString(envoy_data data) {
  if (data.length == 0) {
    return EMPTY_STRING;
  }
  return std::string(const_cast<char*>(reinterpret_cast<const char*>((data.bytes))), data.length);
}

} // namespace Utility
} // namespace Data
} // namespace Envoy
