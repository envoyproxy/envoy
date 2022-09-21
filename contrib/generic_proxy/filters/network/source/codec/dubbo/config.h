#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/codec/dubbo/v3/dubbo.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/codec/dubbo/v3/dubbo.pb.validate.h"

#include "source/extensions/common/dubbo/codec.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

static constexpr absl::string_view DubboProtocolName = "dubbo";

class GenericRequest : public Request {
public:
  GenericRequest(Common::Dubbo::MessageMetadataSharedPtr inner_request)
      : inner_request_(std::move(inner_request)) {
    ASSERT(inner_request_ != nullptr);
    ASSERT(inner_request_->hasContext());
    ASSERT(inner_request_->hasRequest());
  }

  // Request
  absl::string_view protocol() const override { return DubboProtocolName; }
  void forEach(IterateCallback callback) const override;
  absl::optional<absl::string_view> getByKey(absl::string_view key) const override;
  void setByKey(absl::string_view key, absl::string_view val) override;
  void setByReferenceKey(absl::string_view key, absl::string_view val) override;
  void setByReference(absl::string_view key, absl::string_view val) override;
  absl::string_view host() const override { return {}; }
  absl::string_view path() const override { return inner_request_->request().serviceName(); }
  absl::string_view method() const override { return inner_request_->request().methodName(); }

  Common::Dubbo::MessageMetadataSharedPtr inner_request_;
};

class GenericRespnose : public Response {
public:
  GenericRespnose(Common::Dubbo::MessageMetadataSharedPtr inner_response)
      : inner_response_(std::move(inner_response)) {
    ASSERT(inner_response_ != nullptr);
    ASSERT(inner_response_->hasContext());
    ASSERT(inner_response_->hasResponse());
    refreshGenericStatus();
  }

  void refreshGenericStatus();

  // Request
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
  Common::Dubbo::MessageMetadataSharedPtr inner_response_;
};

class DubboRequestDecoder : public RequestDecoder {
public:
  void setDecoderCallback(RequestDecoderCallback& callback) override;
  void decode(Buffer::Instance& buffer) override;
};

class DubboResponseDecoder : public ResponseDecoder {
public:
  void setDecoderCallback(ResponseDecoderCallback& callback) override;
  void decode(Buffer::Instance& buffer) override;
};

class DubboRequestEncoder : public RequestEncoder {
public:
  void encode(const Request&, RequestEncoderCallback& callback) override;
};

class DubboResponseEncoder : public ResponseEncoder {
public:
  void encode(const Response&, ResponseEncoderCallback& callback) override;
};

class DubboMessageCreator : public MessageCreator {
public:
  ResponsePtr response(Status status, const Request& origin_request) override;
};

class DubboCodecFactory : public CodecFactory {
public:
  RequestDecoderPtr requestDecoder() const override {
    return std::make_unique<DubboRequestDecoder>();
  }
  ResponseDecoderPtr responseDecoder() const override {
    return std::make_unique<DubboResponseDecoder>();
  }
  RequestEncoderPtr requestEncoder() const override {
    return std::make_unique<DubboRequestEncoder>();
  }
  ResponseEncoderPtr responseEncoder() const override {
    return std::make_unique<DubboResponseEncoder>();
  }
  MessageCreatorPtr messageCreator() const override {
    return std::make_unique<DubboMessageCreator>();
  }
};

class DubboCodecFactoryConfig : public CodecFactoryConfig {
public:
  CodecFactoryPtr createFactory(const Protobuf::Message& config,
                                Envoy::Server::Configuration::FactoryContext& context) override;

  std::string name() const override { return "envoy.generic_proxy.codec.dubbo"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::network::generic_proxy::codec::dubbo::v3::DubboCodecConfig>();
  }
};

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
