#include "extensions/filters/network/dubbo_proxy/decoder.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Decoder::Decoder(ProtocolPtr&& protocol, DeserializerPtr&& deserializer,
                 DecoderCallbacks& decoder_callbacks)
    : deserializer_(std::move(deserializer)), protocol_(std::move(protocol)),
      decoder_callbacks_(decoder_callbacks) {}

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
      ENVOY_LOG(debug, "dubbo: need more data for {} deserialization", deserializer_->name());
      return;
    }

    if (context_.is_request_) {
      decoder_callbacks_.onRpcInvocation(
          deserializer_->deserializeRpcInvocation(data, context_.body_size_));
      ENVOY_LOG(debug, "dubbo: {} RpcInvocation deserialize ended", deserializer_->name());
    } else {
      decoder_callbacks_.onRpcResult(
          deserializer_->deserializeRpcResult(data, context_.body_size_));
      ENVOY_LOG(debug, "dubbo: {} RpcResult deserialize ended", deserializer_->name());
    }
    decode_ended_ = false;
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
