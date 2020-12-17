#include "library/common/buffer/utility.h"

#include <stdlib.h>

#include "common/buffer/buffer_impl.h"

#include "library/common/buffer/bridge_fragment.h"

namespace Envoy {
namespace Buffer {
namespace Utility {

Buffer::InstancePtr toInternalData(envoy_data data) {
  // This fragment only needs to live until done is called.
  // Therefore, it is sufficient to allocate on the heap, and delete in the done method.
  Buffer::BridgeFragment* fragment = Buffer::BridgeFragment::createBridgeFragment(data);
  InstancePtr buf = std::make_unique<Buffer::OwnedImpl>();
  buf->addBufferFragment(*fragment);
  return buf;
}

envoy_data toBridgeData(Buffer::Instance& data) {
  envoy_data bridge_data;
  bridge_data.length = data.length();
  bridge_data.bytes = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * bridge_data.length));
  data.copyOut(0, bridge_data.length, const_cast<uint8_t*>(bridge_data.bytes));
  data.drain(bridge_data.length);
  bridge_data.release = free;
  bridge_data.context = const_cast<uint8_t*>(bridge_data.bytes);
  return bridge_data;
}

envoy_data copyToBridgeData(absl::string_view str) {
  envoy_data bridge_data;
  bridge_data.length = str.length();
  void* buffer = safe_malloc(sizeof(uint8_t) * bridge_data.length);
  memcpy(buffer, str.data(), str.length());
  bridge_data.bytes = static_cast<uint8_t*>(buffer);
  bridge_data.release = free;
  bridge_data.context = const_cast<uint8_t*>(bridge_data.bytes);
  return bridge_data;
}

envoy_data copyToBridgeData(const Buffer::Instance& data) {
  envoy_data bridge_data;
  bridge_data.length = data.length();
  bridge_data.bytes = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * bridge_data.length));
  data.copyOut(0, bridge_data.length, const_cast<uint8_t*>(bridge_data.bytes));
  bridge_data.release = free;
  bridge_data.context = const_cast<uint8_t*>(bridge_data.bytes);
  return bridge_data;
}

} // namespace Utility
} // namespace Buffer
} // namespace Envoy
