#pragma once

#include <memory>

#include "source/common/common/logger.h"
#include "source/extensions/common/dubbo/codec.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/codecs/dubbo/v3/dubbo.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/codecs/dubbo/v3/dubbo.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

using ProtoConfig =
    envoy::extensions::filters::network::generic_proxy::codecs::dubbo::v3::DubboCodecConfig;

static constexpr absl::string_view DubboProtocolName = "dubbo";

class DubboRequest : public Request {
public:
  DubboRequest(Common::Dubbo::MessageMetadataSharedPtr inner_request)
      : inner_metadata_(std::move(inner_request)) {
    ASSERT(inner_metadata_ != nullptr);
    ASSERT(inner_metadata_->hasContext());
    ASSERT(inner_metadata_->hasRequest());
  }

  // Request
  absl::string_view protocol() const override { return DubboProtocolName; }
  void forEach(IterateCallback callback) const override;
  absl::optional<absl::string_view> getByKey(absl::string_view key) const override;
  void setByKey(absl::string_view key, absl::string_view val) override;
  void setByReferenceKey(absl::string_view key, absl::string_view val) override {
    setByKey(key, val);
  }
  void setByReference(absl::string_view key, absl::string_view val) override { setByKey(key, val); }

  absl::string_view host() const override { return inner_metadata_->request().serviceName(); }
  absl::string_view path() const override { return inner_metadata_->request().serviceName(); }

  absl::string_view method() const override { return inner_metadata_->request().methodName(); }

  Common::Dubbo::MessageMetadataSharedPtr inner_metadata_;
};

class DubboResponse : public Response {
public:
  DubboResponse(Common::Dubbo::MessageMetadataSharedPtr inner_response)
      : inner_metadata_(std::move(inner_response)) {
    ASSERT(inner_metadata_ != nullptr);
    ASSERT(inner_metadata_->hasContext());
    ASSERT(inner_metadata_->hasResponse());
    refreshGenericStatus();
  }

  void refreshGenericStatus();

  // Response.
  absl::string_view protocol() const override { return DubboProtocolName; }
  void forEach(IterateCallback) const override {}
  absl::optional<absl::string_view> getByKey(absl::string_view) const override {
    return absl::nullopt;
  }
  void setByKey(absl::string_view, absl::string_view) override{};
  void setByReferenceKey(absl::string_view, absl::string_view) override {}
  void setByReference(absl::string_view, absl::string_view) override {}

  Status status() const override { return status_; }

  Status status_;
  Common::Dubbo::MessageMetadataSharedPtr inner_metadata_;
};

class DubboCodecBase : public Logger::Loggable<Logger::Id::connection> {
public:
  DubboCodecBase(Common::Dubbo::DubboCodecPtr codec);

  Common::Dubbo::DubboCodecPtr codec_;
};

template <class DecoderType, class MessageType, class CallBackType>
class DubboDecoderBase : public DubboCodecBase, public DecoderType {
public:
  using DubboCodecBase::DubboCodecBase;

  void setDecoderCallback(CallBackType& callback) override { callback_ = &callback; }

  void decode(Buffer::Instance& buffer) override {
    if (metadata_ == nullptr) {
      metadata_ = std::make_shared<Common::Dubbo::MessageMetadata>();
    }

    try {
      Common::Dubbo::DecodeStatus decode_status{Common::Dubbo::DecodeStatus::Success};
      if (!metadata_->hasContext()) {
        ENVOY_LOG(debug, "Dubbo codec: try to decode new dubbo request/response header");
        decode_status = codec_->decodeHeader(buffer, *metadata_);
      }

      if (decode_status == Common::Dubbo::DecodeStatus::Success) {
        ASSERT(metadata_->hasContext());
        ENVOY_LOG(debug, "Dubbo codec: try to decode new dubbo request/response body");
        decode_status = codec_->decodeData(buffer, *metadata_);
      }

      if (decode_status == Common::Dubbo::DecodeStatus::Failure) {
        ENVOY_LOG(warn, "Dubbo codec: unexpected decoding error");
        metadata_.reset();
        callback_->onDecodingFailure();
        return;
      }

      if (decode_status == Common::Dubbo::DecodeStatus::Waiting) {
        ENVOY_LOG(debug, "Dubbo codec: waiting for more input data");
        return;
      }

      ASSERT(decode_status == Common::Dubbo::DecodeStatus::Success);
      callback_->onDecodingSuccess(std::make_unique<MessageType>(std::move(metadata_)));
      metadata_.reset();
    } catch (EnvoyException& error) {
      ENVOY_LOG(warn, "Dubbo codec: decoding error: {}", error.what());
      metadata_.reset();
      callback_->onDecodingFailure();
    }
  }

  Common::Dubbo::MessageMetadataSharedPtr metadata_;
  CallBackType* callback_{};
};

using DubboRequestDecoder = DubboDecoderBase<RequestDecoder, DubboRequest, RequestDecoderCallback>;
using DubboResponseDecoder =
    DubboDecoderBase<ResponseDecoder, DubboResponse, ResponseDecoderCallback>;

class DubboRequestEncoder : public RequestEncoder, public DubboCodecBase {
public:
  using DubboCodecBase::DubboCodecBase;

  void encode(const Request& request, RequestEncoderCallback& callback) override {
    ASSERT(dynamic_cast<const DubboRequest*>(&request) != nullptr);
    const auto* typed_request = static_cast<const DubboRequest*>(&request);

    Buffer::OwnedImpl buffer;
    codec_->encode(buffer, *typed_request->inner_metadata_);
    callback.onEncodingSuccess(buffer, typed_request->inner_metadata_->messageType() !=
                                           Common::Dubbo::MessageType::Oneway);
  }
};

class DubboResponseEncoder : public ResponseEncoder, public DubboCodecBase {
public:
  using DubboCodecBase::DubboCodecBase;

  void encode(const Response& response, ResponseEncoderCallback& callback) override {
    ASSERT(dynamic_cast<const DubboResponse*>(&response) != nullptr);
    const auto* typed_response = static_cast<const DubboResponse*>(&response);

    Buffer::OwnedImpl buffer;
    codec_->encode(buffer, *typed_response->inner_metadata_);
    callback.onEncodingSuccess(buffer, false);
  }
};

class DubboMessageCreator : public MessageCreator {
public:
  ResponsePtr response(Status status, const Request& origin_request) override;
};

class DubboCodecFactory : public CodecFactory {
public:
  RequestDecoderPtr requestDecoder() const override {
    return std::make_unique<DubboRequestDecoder>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
  ResponseDecoderPtr responseDecoder() const override {
    return std::make_unique<DubboResponseDecoder>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
  RequestEncoderPtr requestEncoder() const override {
    return std::make_unique<DubboRequestEncoder>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
  ResponseEncoderPtr responseEncoder() const override {
    return std::make_unique<DubboResponseEncoder>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
  MessageCreatorPtr messageCreator() const override {
    return std::make_unique<DubboMessageCreator>();
  }
};

class DubboCodecFactoryConfig : public CodecFactoryConfig {
public:
  // CodecFactoryConfig
  CodecFactoryPtr
  createCodecFactory(const Protobuf::Message& config,
                     Envoy::Server::Configuration::FactoryContext& context) override;
  std::string name() const override { return "envoy.generic_proxy.codecs.dubbo"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoConfig>();
  }
};

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
