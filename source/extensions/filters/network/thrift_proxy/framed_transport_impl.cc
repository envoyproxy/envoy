#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"

#include "envoy/common/exception.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

bool FramedTransportImpl::decodeFrameStart(Buffer::Instance& buffer) {
  if (buffer.length() < 4) {
    return false;
  }

  int32_t size = BufferHelper::peekI32(buffer);

  if (size <= 0 || size > MaxFrameSize) {
    throw EnvoyException(fmt::format("invalid thrift framed transport frame size {}", size));
  }

  onFrameStart(absl::optional<uint32_t>(static_cast<uint32_t>(size)));

  buffer.drain(4);
  return true;
}

bool FramedTransportImpl::decodeFrameEnd(Buffer::Instance&) {
  onFrameComplete();
  return true;
}

void FramedTransportImpl::encodeFrame(Buffer::Instance& buffer, Buffer::Instance& message) {
  uint64_t size = message.length();
  if (size == 0 || size > MaxFrameSize) {
    throw EnvoyException(fmt::format("invalid thrift framed transport frame size {}", size));
  }

  int32_t thrift_size = static_cast<int32_t>(size);

  BufferHelper::writeI32(buffer, thrift_size);
  buffer.move(message);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
