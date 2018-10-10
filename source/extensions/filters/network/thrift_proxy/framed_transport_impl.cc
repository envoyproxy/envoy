#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"

#include "envoy/common/exception.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

bool FramedTransportImpl::decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) {
  UNREFERENCED_PARAMETER(metadata);

  if (buffer.length() < 4) {
    return false;
  }

  int32_t thrift_size = buffer.peekBEInt<int32_t>();

  if (thrift_size <= 0 || thrift_size > MaxFrameSize) {
    throw EnvoyException(fmt::format("invalid thrift framed transport frame size {}", thrift_size));
  }

  buffer.drain(4);

  metadata.setFrameSize(static_cast<uint32_t>(thrift_size));
  return true;
}

bool FramedTransportImpl::decodeFrameEnd(Buffer::Instance&) { return true; }

void FramedTransportImpl::encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                                      Buffer::Instance& message) {
  UNREFERENCED_PARAMETER(metadata);

  uint64_t size = message.length();
  if (size == 0 || size > MaxFrameSize) {
    throw EnvoyException(fmt::format("invalid thrift framed transport frame size {}", size));
  }

  int32_t thrift_size = static_cast<int32_t>(size);

  buffer.writeBEInt<int32_t>(thrift_size);
  buffer.move(message);
}

class FramedTransportConfigFactory : public TransportFactoryBase<FramedTransportImpl> {
public:
  FramedTransportConfigFactory() : TransportFactoryBase(TransportNames::get().FRAMED) {}
};

/**
 * Static registration for the framed transport. @see RegisterFactory.
 */
static Registry::RegisterFactory<FramedTransportConfigFactory, NamedTransportConfigFactory>
    register_;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
