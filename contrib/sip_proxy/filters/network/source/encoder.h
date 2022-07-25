#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "contrib/sip_proxy/filters/network/source/metadata.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
class Encoder : public Logger::Loggable<Logger::Id::filter> {
public:
  virtual ~Encoder() = default;
  virtual void encode(const MessageMetadataSharedPtr& metadata, Buffer::Instance& out) PURE;
};

class EncoderImpl : public Encoder {
public:
  void encode(const MessageMetadataSharedPtr& metadata, Buffer::Instance& out) override;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
