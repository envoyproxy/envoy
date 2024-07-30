#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/codecs/kafka/v3/kafka.pb.h"
#include "contrib/kafka/filters/network/source/request_codec.h"
#include "contrib/kafka/filters/network/source/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Kafka {

using ProtoConfig =
    envoy::extensions::filters::network::generic_proxy::codecs::kafka::v3::KafkaCodecConfig;

class KafkaRequestFrame : public GenericProxy::StreamRequest {
public:
  KafkaRequestFrame(NetworkFilters::Kafka::AbstractRequestSharedPtr request)
      : request_(std::move(request)) {
    ASSERT(request_ != nullptr);
  }

  FrameFlags frameFlags() const override {
    if (request_ == nullptr) {
      return FrameFlags{};
    }
    return FrameFlags{static_cast<uint64_t>(request_->request_header_.correlation_id_)};
  }

  absl::string_view protocol() const override { return "kafka"; }

  NetworkFilters::Kafka::AbstractRequestSharedPtr request_;
};

class KafkaResponseFrame : public GenericProxy::StreamResponse {
public:
  KafkaResponseFrame(NetworkFilters::Kafka::AbstractResponseSharedPtr response)
      : response_(std::move(response)) {}

  FrameFlags frameFlags() const override {
    if (response_ == nullptr) {
      return FrameFlags{};
    }
    return FrameFlags{static_cast<uint64_t>(response_->metadata_.correlation_id_)};
  }

  absl::string_view protocol() const override { return "kafka"; }

  NetworkFilters::Kafka::AbstractResponseSharedPtr response_;
};

class KafkaRequestCallbacks : public NetworkFilters::Kafka::RequestCallback,
                              public Envoy::Logger::Loggable<Envoy::Logger::Id::kafka> {
public:
  KafkaRequestCallbacks(GenericProxy::ServerCodecCallbacks& callbacks) : callbacks_(callbacks) {}

  void onMessage(NetworkFilters::Kafka::AbstractRequestSharedPtr request) override {
    ENVOY_CONN_LOG(debug, "Kafka codec: new request from downstream client",
                   callbacks_.connection().ref());
    callbacks_.onDecodingSuccess(std::make_unique<KafkaRequestFrame>(std::move(request)));
  }

  void onFailedParse(NetworkFilters::Kafka::RequestParseFailureSharedPtr) override {
    ENVOY_CONN_LOG(debug, "Kafka codec: failed to parse request from downstream client",
                   callbacks_.connection().ref());
    callbacks_.onDecodingFailure();
  }

  GenericProxy::ServerCodecCallbacks& callbacks_;
};

class KafkaResponseCallbacks : public NetworkFilters::Kafka::ResponseCallback,
                               public Envoy::Logger::Loggable<Envoy::Logger::Id::kafka> {
public:
  KafkaResponseCallbacks(GenericProxy::ClientCodecCallbacks& callbacks) : callbacks_(callbacks) {}

  void onMessage(NetworkFilters::Kafka::AbstractResponseSharedPtr response) override {
    ENVOY_CONN_LOG(debug, "Kafka codec: new response from upstream server",
                   callbacks_.connection().ref());
    callbacks_.onDecodingSuccess(std::make_unique<KafkaResponseFrame>(std::move(response)));
  }

  void onFailedParse(NetworkFilters::Kafka::ResponseMetadataSharedPtr) override {
    ENVOY_CONN_LOG(debug, "Kafka codec: failed to parse response from upstream server",
                   callbacks_.connection().ref());
    callbacks_.onDecodingFailure();
  }

  GenericProxy::ClientCodecCallbacks& callbacks_;
};

class KafkaServerCodec : public GenericProxy::ServerCodec,
                         public Envoy::Logger::Loggable<Envoy::Logger::Id::kafka> {
public:
  KafkaServerCodec();

  void setCodecCallbacks(GenericProxy::ServerCodecCallbacks& callbacks) override;
  void decode(Envoy::Buffer::Instance& buffer, bool end_stream) override;
  GenericProxy::EncodingResult encode(const GenericProxy::StreamFrame& frame,
                                      GenericProxy::EncodingContext& ctx) override;
  GenericProxy::ResponsePtr respond(absl::Status, absl::string_view,
                                    const GenericProxy::Request&) override;

  Envoy::Buffer::OwnedImpl request_buffer_;
  Envoy::Buffer::OwnedImpl response_buffer_;

  NetworkFilters::Kafka::RequestDecoderSharedPtr request_decoder_;
  NetworkFilters::Kafka::ResponseEncoder response_encoder_;

  std::shared_ptr<KafkaRequestCallbacks> request_callbacks_;
};

class KafkaClientCodec : public GenericProxy::ClientCodec,
                         public Envoy::Logger::Loggable<Envoy::Logger::Id::kafka> {
public:
  KafkaClientCodec();

  void setCodecCallbacks(GenericProxy::ClientCodecCallbacks& callbacks) override;
  void decode(Envoy::Buffer::Instance& buffer, bool end_stream) override;
  GenericProxy::EncodingResult encode(const GenericProxy::StreamFrame& frame,
                                      GenericProxy::EncodingContext& ctx) override;

  Envoy::Buffer::OwnedImpl request_buffer_;
  Envoy::Buffer::OwnedImpl response_buffer_;

  NetworkFilters::Kafka::ResponseDecoderSharedPtr response_decoder_;
  NetworkFilters::Kafka::RequestEncoder request_encoder_;

  std::shared_ptr<KafkaResponseCallbacks> response_callbacks_;
};

class KafkaCodecFactory : public GenericProxy::CodecFactory {
public:
  GenericProxy::ClientCodecPtr createClientCodec() const override {
    return std::make_unique<KafkaClientCodec>();
  }

  GenericProxy::ServerCodecPtr createServerCodec() const override {
    return std::make_unique<KafkaServerCodec>();
  }
};

class KafkaCodecFactoryConfig : public GenericProxy::CodecFactoryConfig {
public:
  // CodecFactoryConfig
  GenericProxy::CodecFactoryPtr
  createCodecFactory(const Envoy::Protobuf::Message& config,
                     Envoy::Server::Configuration::ServerFactoryContext& context) override;
  std::string name() const override { return "envoy.generic_proxy.codecs.kafka"; }
  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoConfig>();
  }
};

} // namespace Kafka
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
