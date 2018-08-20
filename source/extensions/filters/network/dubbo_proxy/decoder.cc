#include "extensions/filters/network/dubbo_proxy/decoder.h"

#include <unordered_map>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Decoder::Decoder(ProtocolPtr&& protocol, SerializationPtr&& serializer)
    : serializer_(std::move(serializer)), protocol_(std::move(protocol)) {}

void Decoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "dubbo: {} bytes available", data.length());
  while (true) {
    if (!decode_ended_) {
      if (!protocol_->decode(data, &context_)) {
        ENVOY_LOG(debug, "dubbo: need more data for {} protocol", protocol_->name());
        return;
      }

      decode_ended_ = true;
      ENVOY_LOG(debug, "dubbo: {} protocol decode ended", protocol_->name());
    }

    ENVOY_LOG(debug, "dubbo: expected body size is {}", context_.body_size_);

    if (data.length() < context_.body_size_) {
      ENVOY_LOG(debug, "dubbo: need more data for {} deserialize", serializer_->name());
      return;
    }

    if (context_.is_request_) {
      serializer_->deserializeRpcInvocation(data, context_.body_size_);
      ENVOY_LOG(debug, "dubbo: {} RpcInvocation deserialize ended", serializer_->name());
    } else {
      serializer_->deserializeRpcResult(data, context_.body_size_);
      ENVOY_LOG(debug, "dubbo: {} RpcResult deserialize ended", serializer_->name());
    }
    decode_ended_ = false;
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
