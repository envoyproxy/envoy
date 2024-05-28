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
  absl::optional<absl::string_view> get(absl::string_view key) const override;
  void set(absl::string_view key, absl::string_view val) override;
  absl::string_view host() const override { return inner_metadata_->request().service(); }
  absl::string_view path() const override { return inner_metadata_->request().service(); }
  absl::string_view method() const override { return inner_metadata_->request().method(); }
  void erase(absl::string_view key) override;

  // StreamFrame
  FrameFlags frameFlags() const override { return stream_frame_flags_; }

  FrameFlags stream_frame_flags_;

  Common::Dubbo::MessageMetadataSharedPtr inner_metadata_;
};

class DubboResponse : public Response {
public:
  DubboResponse(Common::Dubbo::MessageMetadataSharedPtr inner_response)
      : inner_metadata_(std::move(inner_response)) {
    ASSERT(inner_metadata_ != nullptr);
    ASSERT(inner_metadata_->hasContext());
    ASSERT(inner_metadata_->hasResponse());
    refreshStatus();
  }

  void refreshStatus();

  // Response.
  absl::string_view protocol() const override { return DubboProtocolName; }
  StreamStatus status() const override { return status_; }

  // StreamFrame
  FrameFlags frameFlags() const override { return stream_frame_flags_; }

  FrameFlags stream_frame_flags_;

  StreamStatus status_;
  Common::Dubbo::MessageMetadataSharedPtr inner_metadata_;
};

class DubboCodecBase : public Logger::Loggable<Logger::Id::connection> {
public:
  DubboCodecBase(Common::Dubbo::DubboCodecPtr codec);

  Common::Dubbo::DubboCodecPtr codec_;
};

template <class CodecType, class DecoderMessageType, class EncoderMessageType, class CallBackType>
class DubboDecoderBase : public DubboCodecBase, public CodecType {
public:
  using DubboCodecBase::DubboCodecBase;

  void setCodecCallbacks(CallBackType& callback) override { callback_ = &callback; }

  Common::Dubbo::DecodeStatus decodeOne(Buffer::Instance& buffer) {
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
        return Common::Dubbo::DecodeStatus::Failure;
      }

      if (decode_status == Common::Dubbo::DecodeStatus::Waiting) {
        ENVOY_LOG(debug, "Dubbo codec: waiting for more input data");
        return Common::Dubbo::DecodeStatus::Waiting;
      }

      ASSERT(decode_status == Common::Dubbo::DecodeStatus::Success);

      if (metadata_->context().heartbeat()) {
        Buffer::OwnedImpl heartbeat_response;

        ENVOY_LOG(debug, "Dubbo codec: heartbeat from downstream/upstream");
        constexpr char first_four_bytes[] = {'\xda', '\xbb', '\x22', 20};
        heartbeat_response.add(first_four_bytes, 4);

        heartbeat_response.writeBEInt<int64_t>(metadata_->requestId());
        heartbeat_response.writeBEInt<int32_t>(1);
        heartbeat_response.writeByte('N');

        metadata_.reset();
        callback_->writeToConnection(heartbeat_response);

        return Common::Dubbo::DecodeStatus::Success;
      }

      auto message = std::make_unique<DecoderMessageType>(metadata_);
      message->stream_frame_flags_ = {{static_cast<uint64_t>(metadata_->requestId()),
                                       !metadata_->context().isTwoWay(), false,
                                       metadata_->context().heartbeat()},
                                      true};
      metadata_.reset();
      callback_->onDecodingSuccess(std::move(message));

      return Common::Dubbo::DecodeStatus::Success;
    } catch (const EnvoyException& error) {
      ENVOY_LOG(warn, "Dubbo codec: decoding error: {}", error.what());

      metadata_.reset();
      callback_->onDecodingFailure();

      return Common::Dubbo::DecodeStatus::Failure;
    }
  }

  void decode(Buffer::Instance& buffer, bool) override {
    while (buffer.length() > 0) {
      // Continue decoding if the buffer has more data and the previous decoding is
      // successful.
      if (decodeOne(buffer) != Common::Dubbo::DecodeStatus::Success) {
        break;
      }
    }
  }

  void encode(const StreamFrame& frame, EncodingCallbacks& callbacks) override {
    ASSERT(dynamic_cast<const EncoderMessageType*>(&frame) != nullptr);
    const auto* typed_message = static_cast<const EncoderMessageType*>(&frame);

    Buffer::OwnedImpl buffer;
    codec_->encode(buffer, *typed_message->inner_metadata_);
    callbacks.onEncodingSuccess(buffer, true);
  }

  Common::Dubbo::MessageMetadataSharedPtr metadata_;
  CallBackType* callback_{};
};

class DubboServerCodec
    : public DubboDecoderBase<ServerCodec, DubboRequest, DubboResponse, ServerCodecCallbacks> {
public:
  using DubboDecoderBase::DubboDecoderBase;

  ResponsePtr respond(absl::Status status, absl::string_view data, const Request& request) override;
};

class DubboClientCodec
    : public DubboDecoderBase<ClientCodec, DubboResponse, DubboRequest, ClientCodecCallbacks> {
public:
  using DubboDecoderBase::DubboDecoderBase;
};

class DubboCodecFactory : public CodecFactory {
public:
  ServerCodecPtr createServerCodec() const override {
    return std::make_unique<DubboServerCodec>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
  ClientCodecPtr createClientCodec() const override {
    return std::make_unique<DubboClientCodec>(
        Common::Dubbo::DubboCodec::codecFromSerializeType(Common::Dubbo::SerializeType::Hessian2));
  }
};

class DubboCodecFactoryConfig : public CodecFactoryConfig {
public:
  // CodecFactoryConfig
  CodecFactoryPtr
  createCodecFactory(const Protobuf::Message& config,
                     Envoy::Server::Configuration::ServerFactoryContext& context) override;
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
