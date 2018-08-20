#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Decoder encapsulates a configured and ProtocolPtr and SerializationPtr.
 */
class Decoder : public Logger::Loggable<Logger::Id::dubbo> {
public:
  Decoder(ProtocolPtr&& protocol, SerializationPtr&& serializer);

  /**
   * Drains data from the given buffer
   *
   * @param data a Buffer containing Dubbo protocol data
   * @throw EnvoyException on Dubbo protocol errors
   */
  void onData(Buffer::Instance& data);

  const Serialization& serializer() { return *serializer_; }
  const Protocol& protocol() { return *protocol_; }

private:
  SerializationPtr serializer_;
  ProtocolPtr protocol_;
  bool decode_ended_ = false;
  Protocol::Context context_;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
