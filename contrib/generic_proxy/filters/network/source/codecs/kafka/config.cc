#include "contrib/generic_proxy/filters/network/source/codecs/kafka/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Kafka {

KafkaServerCodec::KafkaServerCodec() : response_encoder_(response_buffer_) {}

void KafkaServerCodec::setCodecCallbacks(GenericProxy::ServerCodecCallbacks& callbacks) {
  request_callbacks_ = std::make_shared<KafkaRequestCallbacks>(callbacks);
  request_decoder_ = std::make_shared<NetworkFilters::Kafka::RequestDecoder>(
      std::vector<NetworkFilters::Kafka::RequestCallbackSharedPtr>{request_callbacks_});
}

void KafkaServerCodec::decode(Envoy::Buffer::Instance& buffer, bool) {
  request_buffer_.move(buffer);
  request_decoder_->onData(request_buffer_);
  // All data has been consumed, so we can drain the buffer.
  request_buffer_.drain(request_buffer_.length());
}

GenericProxy::EncodingResult KafkaServerCodec::encode(const GenericProxy::StreamFrame& frame,
                                                      GenericProxy::EncodingContext&) {
  auto* typed_response = dynamic_cast<const KafkaResponseFrame*>(&frame);
  if (typed_response == nullptr) {
    ENVOY_LOG(error, "Kafka codec: invalid response frame type and cannot encode");
    return absl::InvalidArgumentError("Invalid response frame type");
  }
  if (typed_response->response_ != nullptr) {
    response_encoder_.encode(*typed_response->response_);
  } else {
    ENVOY_LOG(error, "Kafka codec: invalid empty response frame");
    return absl::InvalidArgumentError("Invalid empty response frame");
  }

  const uint64_t encoded_size = response_buffer_.length();

  // Write the encoded data to the connection and clean the buffer for the next encoding.
  request_callbacks_->callbacks_.writeToConnection(response_buffer_);
  // All data should be consumed by the generic proxy and send to the network.
  ASSERT(response_buffer_.length() == 0);

  return encoded_size;
}
GenericProxy::ResponsePtr KafkaServerCodec::respond(absl::Status, absl::string_view,
                                                    const GenericProxy::Request&) {
  return std::make_unique<KafkaResponseFrame>(nullptr);
};

KafkaClientCodec::KafkaClientCodec() : request_encoder_(request_buffer_) {}

void KafkaClientCodec::setCodecCallbacks(GenericProxy::ClientCodecCallbacks& callbacks) {
  response_callbacks_ = std::make_shared<KafkaResponseCallbacks>(callbacks);
  response_decoder_ = std::make_shared<NetworkFilters::Kafka::ResponseDecoder>(
      std::vector<NetworkFilters::Kafka::ResponseCallbackSharedPtr>{response_callbacks_});
}

void KafkaClientCodec::decode(Envoy::Buffer::Instance& buffer, bool) {
  response_buffer_.move(buffer);
  response_decoder_->onData(response_buffer_);
  // All data has been consumed, so we can drain the buffer.
  response_buffer_.drain(response_buffer_.length());
}

GenericProxy::EncodingResult KafkaClientCodec::encode(const GenericProxy::StreamFrame& frame,
                                                      GenericProxy::EncodingContext&) {
  auto* typed_request = dynamic_cast<const KafkaRequestFrame*>(&frame);
  if (typed_request == nullptr) {
    ENVOY_LOG(error, "Kafka codec: invalid request frame type and cannot encode");
    return absl::InvalidArgumentError("Invalid request frame type");
  }
  response_decoder_->expectResponse(typed_request->request_->request_header_.correlation_id_,
                                    typed_request->request_->request_header_.api_key_,
                                    typed_request->request_->request_header_.api_version_);
  request_encoder_.encode(*typed_request->request_);

  const uint64_t encoded_size = request_buffer_.length();

  // Write the encoded data to the connection and clean the buffer for the next encoding.
  response_callbacks_->callbacks_.writeToConnection(request_buffer_);
  // All data should be consumed by the generic proxy and send to the network.
  ASSERT(request_buffer_.length() == 0);

  return encoded_size;
}

CodecFactoryPtr
KafkaCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                            Envoy::Server::Configuration::ServerFactoryContext&) {
  return std::make_unique<KafkaCodecFactory>();
}

REGISTER_FACTORY(KafkaCodecFactoryConfig, CodecFactoryConfig);

} // namespace Kafka
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
