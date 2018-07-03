#include "extensions/filters/network/thrift_proxy/transport_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

bool AutoTransportImpl::decodeFrameStart(Buffer::Instance& buffer) {
  if (transport_ == nullptr) {
    // Not enough data to select a transport.
    if (buffer.length() < 8) {
      return false;
    }

    int32_t size = BufferHelper::peekI32(buffer);
    uint16_t proto_start = BufferHelper::peekU16(buffer, 4);

    if (size > 0 && size <= FramedTransportImpl::MaxFrameSize) {
      // TODO(zuercher): Spec says max size is 16,384,000 (0xFA0000). Apache C++ TFramedTransport
      // is configurable, but defaults to 256 MB (0x1000000). THeaderTransport will take up to ~1GB
      // (0x3FFFFFFF) when it falls back to framed mode.
      if (BinaryProtocolImpl::isMagic(proto_start) || CompactProtocolImpl::isMagic(proto_start)) {
        setTransport(std::make_unique<FramedTransportImpl>(callbacks_));
      }
    } else {
      // Check for sane unframed protocol.
      proto_start = static_cast<uint16_t>((size >> 16) & 0xFFFF);
      if (BinaryProtocolImpl::isMagic(proto_start) || CompactProtocolImpl::isMagic(proto_start)) {
        setTransport(std::make_unique<UnframedTransportImpl>(callbacks_));
      }
    }

    if (transport_ == nullptr) {
      uint8_t start[9] = {0};
      buffer.copyOut(0, 8, start);

      throw EnvoyException(fmt::format("unknown thrift auto transport frame start "
                                       "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                                       start[0], start[1], start[2], start[3], start[4], start[5],
                                       start[6], start[7]));
    }
  }

  return transport_->decodeFrameStart(buffer);
}

bool AutoTransportImpl::decodeFrameEnd(Buffer::Instance& buffer) {
  RELEASE_ASSERT(transport_ != nullptr);
  return transport_->decodeFrameEnd(buffer);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
