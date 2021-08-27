#include "source/extensions/filters/network/thrift_proxy/auto_protocol_impl.h"

#include <algorithm>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/byte_order.h"
#include "source/common/common/macros.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "source/extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/twitter_protocol_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

void AutoProtocolImpl::setType(ProtocolType type) {
  if (!protocol_) {
    switch (type) {
    case ProtocolType::Binary:
      setProtocol(std::make_unique<BinaryProtocolImpl>());
      break;
    case ProtocolType::Compact:
      setProtocol(std::make_unique<CompactProtocolImpl>());
      break;
    case ProtocolType::Twitter:
      setProtocol(std::make_unique<TwitterProtocolImpl>());
      break;
    default:
      // Ignored: attempt protocol detection.
      break;
    }
  }
}

bool AutoProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  if (protocol_ == nullptr) {
    if (buffer.length() < 2) {
      return false;
    }

    uint16_t version = buffer.peekBEInt<uint16_t>();
    if (BinaryProtocolImpl::isMagic(version)) {
      // 12 bytes is the minimum length for message-begin in the binary protocol.
      if (buffer.length() < BinaryProtocolImpl::MinMessageBeginLength) {
        return false;
      }

      // The first message in the twitter protocol is always an upgrade request, so we use as
      // much of the buffer as possible to detect the upgrade message. If we guess wrong,
      // TwitterProtocolImpl will still fall back to binary protocol.
      if (TwitterProtocolImpl::isUpgradePrefix(buffer)) {
        setType(ProtocolType::Twitter);
      } else {
        setType(ProtocolType::Binary);
      }
    } else if (CompactProtocolImpl::isMagic(version)) {
      setType(ProtocolType::Compact);
    }

    if (!protocol_) {
      throw EnvoyException(
          fmt::format("unknown thrift auto protocol message start {:04x}", version));
    }
  }

  return protocol_->readMessageBegin(buffer, metadata);
}

bool AutoProtocolImpl::readMessageEnd(Buffer::Instance& buffer) {
  RELEASE_ASSERT(protocol_ != nullptr, "");
  return protocol_->readMessageEnd(buffer);
}

class AutoProtocolConfigFactory : public ProtocolFactoryBase<AutoProtocolImpl> {
public:
  AutoProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().AUTO) {}
};

/**
 * Static registration for the auto protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(AutoProtocolConfigFactory, NamedProtocolConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
