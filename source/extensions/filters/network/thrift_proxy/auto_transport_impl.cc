#include "source/extensions/filters/network/thrift_proxy/auto_transport_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "source/extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

bool AutoTransportImpl::decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) {
  if (transport_ == nullptr) {
    // Not enough data to select a transport.
    if (buffer.length() < 8) {
      return false;
    }

    int32_t size = buffer.peekBEInt<int32_t>();
    uint16_t proto_start = buffer.peekBEInt<uint16_t>(4);

    // Currently, transport detection depends on the following:
    // 1. Protocol may only be binary or compact, which start with 0x8001 or 0x8201.
    // 2. If unframed transport, size will appear negative due to leading protocol bytes.
    // 3. If header transport, size is followed by 0x0FFF which is distinct from leading
    //    protocol bytes.
    // 4. For framed transport, size is followed by protocol bytes.
    if (size > 0 && size <= HeaderTransportImpl::MaxFrameSize &&
        HeaderTransportImpl::isMagic(proto_start)) {
      setTransport(std::make_unique<HeaderTransportImpl>());
    } else if (size > 0 && size <= FramedTransportImpl::MaxFrameSize) {
      // TODO(zuercher): Spec says max size is 16,384,000 (0xFA0000). Apache C++ TFramedTransport
      // is configurable, but defaults to 256 MB (0x1000000).
      if (BinaryProtocolImpl::isMagic(proto_start) || CompactProtocolImpl::isMagic(proto_start)) {
        setTransport(std::make_unique<FramedTransportImpl>());
      }
    } else {
      // Check for sane unframed protocol.
      proto_start = static_cast<uint16_t>((size >> 16) & 0xFFFF);
      if (BinaryProtocolImpl::isMagic(proto_start) || CompactProtocolImpl::isMagic(proto_start)) {
        setTransport(std::make_unique<UnframedTransportImpl>());
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

  return transport_->decodeFrameStart(buffer, metadata);
}

bool AutoTransportImpl::decodeFrameEnd(Buffer::Instance& buffer) {
  RELEASE_ASSERT(transport_ != nullptr, "");
  return transport_->decodeFrameEnd(buffer);
}

void AutoTransportImpl::encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                                    Buffer::Instance& message) {
  RELEASE_ASSERT(transport_ != nullptr, "auto transport cannot encode before transport detection");
  transport_->encodeFrame(buffer, metadata, message);
}

class AutoTransportConfigFactory : public TransportFactoryBase<AutoTransportImpl> {
public:
  AutoTransportConfigFactory() : TransportFactoryBase(TransportNames::get().AUTO) {}
};

/**
 * Static registration for the auto transport. @see RegisterFactory.
 */
REGISTER_FACTORY(AutoTransportConfigFactory, NamedTransportConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
