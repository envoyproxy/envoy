#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}
  virtual void onRpcInvocation(RpcInvocationPtr&& invo) PURE;
  virtual void onRpcResult(RpcResultPtr&& res) PURE;
};

/**
 * Decoder encapsulates a configured and ProtocolPtr and SerializationPtr.
 */
class Decoder : public Logger::Loggable<Logger::Id::dubbo> {
public:
  Decoder(ProtocolPtr&& protocol, DeserializerPtr&& deserializer,
          DecoderCallbacks& decoder_callbacks);

  /**
   * Drains data from the given buffer
   *
   * @param data a Buffer containing Dubbo protocol data
   * @throw EnvoyException on Dubbo protocol errors
   */
  void onData(Buffer::Instance& data);

  const Deserializer& serializer() { return *deserializer_; }
  const Protocol& protocol() { return *protocol_; }

private:
  DeserializerPtr deserializer_;
  ProtocolPtr protocol_;
  bool decode_ended_ = false;
  Protocol::Context context_;
  DecoderCallbacks& decoder_callbacks_;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
