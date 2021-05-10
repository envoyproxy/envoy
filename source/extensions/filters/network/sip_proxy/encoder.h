#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "extensions/filters/network/sip_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
class Encoder {
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
