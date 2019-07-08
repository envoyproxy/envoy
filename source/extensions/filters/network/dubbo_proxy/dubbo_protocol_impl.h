#pragma once

#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DubboProtocolImpl : public Protocol {
public:
  DubboProtocolImpl() = default;
  const std::string& name() const override { return ProtocolNames::get().fromType(type()); }
  ProtocolType type() const override { return ProtocolType::Dubbo; }
  bool decode(Buffer::Instance& buffer, Protocol::Context* context,
              MessageMetadataSharedPtr metadata) override;
  bool encode(Buffer::Instance& buffer, int32_t body_size,
              const MessageMetadata& metadata) override;

  static constexpr uint8_t MessageSize = 16;
  static constexpr int32_t MaxBodySize = 16 * 1024 * 1024;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
