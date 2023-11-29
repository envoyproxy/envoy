#include <cstdint>
#include <memory>

#include "source/extensions/common/dubbo/message_impl.h"

#include "test/extensions/common/dubbo/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "contrib/generic_proxy/filters/network/source/codecs/dubbo/config.h"
#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {
namespace {

using testing::_;
using testing::ByMove;
using testing::Return;

using namespace Common::Dubbo;

MessageMetadataSharedPtr createDubboRequst(bool one_way_request) {
  auto request = std::make_unique<RpcRequestImpl>();
  request->setServiceName("fake_service");
  request->setMethodName("fake_method");
  request->setServiceVersion("fake_version");
  request->setParametersLazyCallback([]() -> RpcRequestImpl::ParametersPtr {
    return std::make_unique<RpcRequestImpl::Parameters>();
  });
  request->setAttachmentLazyCallback([]() -> RpcRequestImpl::AttachmentPtr {
    auto map = std::make_unique<RpcRequestImpl::Attachment::Map>();
    Hessian2::ObjectPtr key_o = std::make_unique<Hessian2::StringObject>("group");
    Hessian2::ObjectPtr val_o = std::make_unique<Hessian2::StringObject>("fake_group");

    map->toMutableUntypedMap().value().get().emplace(std::move(key_o), std::move(val_o));
    return std::make_unique<RpcRequestImpl::Attachment>(std::move(map), 0);
  });

  auto context = std::make_unique<Context>();
  context->setMessageType(one_way_request ? MessageType::Oneway : MessageType::Request);
  context->setRequestId(123456);
  context->setSerializeType(SerializeType::Hessian2);

  auto metadata = std::make_shared<MessageMetadata>();
  metadata->setContext(std::move(context));
  metadata->setRequest(std::move(request));
  return metadata;
}

MessageMetadataSharedPtr createDubboResponse(DubboRequest& request, ResponseStatus status,
                                             absl::optional<RpcResponseType> type) {
  return DirectResponseUtil::localResponse(*request.inner_metadata_, status, type, "anything");
}

TEST(DubboRequestTest, DubboRequestTest) {
  DubboRequest request(createDubboRequst(false));

  // Static atrributes test.
  { EXPECT_EQ("dubbo", request.protocol()); }

  // Basic atrributes test.
  {
    EXPECT_EQ("fake_service", request.host());
    EXPECT_EQ("fake_service", request.path());
    EXPECT_EQ("fake_method", request.method());
    EXPECT_EQ("fake_version", request.get("version").value());
  }

  // Get and set headers.
  {
    EXPECT_EQ("fake_group", request.get("group").value());

    EXPECT_EQ(false, request.get("custom_key").has_value());

    request.set("custom_key", "custom_value");
    EXPECT_EQ("custom_value", request.get("custom_key").value());
  }

  // Iterate headers.
  {
    size_t attachment_size = 0;
    request.forEach([&attachment_size](absl::string_view, absl::string_view) {
      attachment_size++;
      return true;
    });
    // Version is not part of attachments. So there are only 2 attachments.
    EXPECT_EQ(2, attachment_size);
  }
}

TEST(DubboResponseTest, DubboResponseTest) {
  DubboRequest request(createDubboRequst(false));

  // Static atrributes test.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));
    EXPECT_EQ("dubbo", response.protocol());
  }

  // Response status check.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));
    EXPECT_EQ(StatusCode::kOk, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithException));
    EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
    EXPECT_EQ("exception_via_upstream", response.status().message());
  }
  {
    DubboResponse response(createDubboResponse(
        request, ResponseStatus::Ok, RpcResponseType::ResponseWithExceptionWithAttachments));
    EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
    EXPECT_EQ("exception_via_upstream", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ClientTimeout, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnknown, response.status().code());
    EXPECT_EQ("ClientTimeout", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServerTimeout, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnknown, response.status().code());
    EXPECT_EQ(StatusCode::kUnknown, response.status().code());
    EXPECT_EQ("ServerTimeout", response.status().message());
  }
  {
    DubboResponse response(createDubboResponse(request, ResponseStatus::BadRequest, absl::nullopt));
    EXPECT_EQ(StatusCode::kInvalidArgument, response.status().code());
    EXPECT_EQ("BadRequest", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::BadResponse, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnknown, response.status().code());
    EXPECT_EQ("BadResponse", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServiceNotFound, absl::nullopt));
    EXPECT_EQ(StatusCode::kNotFound, response.status().code());
    EXPECT_EQ("ServiceNotFound", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServiceError, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
    EXPECT_EQ("ServiceError", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServerError, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
    EXPECT_EQ("ServerError", response.status().message());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ClientError, absl::nullopt));
    EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
    EXPECT_EQ("ClientError", response.status().message());
  }
  {
    DubboResponse response(createDubboResponse(
        request, ResponseStatus::ServerThreadpoolExhaustedError, absl::nullopt));
    EXPECT_EQ(StatusCode::kResourceExhausted, response.status().code());
    EXPECT_EQ("ServerThreadpoolExhaustedError", response.status().message());
  }

  // Getter and setter do nothing for response.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));

    EXPECT_EQ(false, response.get("custom_key").has_value());
    response.set("custom_key", "custom_value");
    EXPECT_EQ(false, response.get("custom_key").has_value());
  }

  // Iterate headers.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));

    size_t attachment_size = 0;
    response.forEach([&attachment_size](absl::string_view, absl::string_view) {
      attachment_size++;
      return true;
    });
    EXPECT_EQ(0, attachment_size);
  }
}

TEST(DubboServerCodecTest, DubboServerCodecTest) {
  auto codec = std::make_unique<DubboCodec>();
  codec->initilize(std::make_unique<MockSerializer>());

  MockServerCodecCallbacks callbacks;
  DubboServerCodec server_codec(std::move(codec));
  server_codec.setCodecCallbacks(callbacks);

  auto raw_serializer = const_cast<MockSerializer*>(
      dynamic_cast<const MockSerializer*>(server_codec.codec_->serializer().get()));

  // Decode failure.
  {
    server_codec.metadata_.reset();
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(0);
    buffer.writeBEInt<int64_t>(0);

    EXPECT_CALL(callbacks, onDecodingFailure());
    server_codec.decode(buffer, false);
  }

  // Waiting for header.
  {
    server_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));

    // No enough header bytes and do nothing.
    server_codec.decode(buffer, false);
  }

  // Waiting for data.
  {
    server_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(8);

    // No enough body bytes and do nothing.
    server_codec.decode(buffer, false);
  }

  // Decode request.
  {
    server_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(8);
    buffer.add("anything");

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _))
        .WillOnce(Return(ByMove(std::make_unique<RpcRequestImpl>())));

    EXPECT_CALL(callbacks, onDecodingSuccess(_));
    server_codec.decode(buffer, false);
  }

  // Encode response.
  {

    MockEncodingCallbacks encoding_callbacks;
    DubboRequest request(createDubboRequst(false));
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _));
    EXPECT_CALL(encoding_callbacks, onEncodingSuccess(_, _));

    server_codec.encode(response, encoding_callbacks);
  }

  {
    Status status = absl::OkStatus();
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto* typed_inner_response =
        static_cast<RpcResponseImpl*>(&typed_response->inner_metadata_->mutableResponse());

    EXPECT_EQ(ResponseStatus::Ok, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(RpcResponseType::ResponseWithException, typed_inner_response->responseType().value());
    EXPECT_EQ("exception_via_proxy", typed_inner_response->localRawMessage().value());
  }

  {
    Status status(StatusCode::kInvalidArgument, "test_message");
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto* typed_inner_response =
        static_cast<RpcResponseImpl*>(&typed_response->inner_metadata_->mutableResponse());

    EXPECT_EQ(ResponseStatus::BadRequest, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(false, typed_inner_response->responseType().has_value());
    EXPECT_EQ("test_message", typed_inner_response->localRawMessage().value());
  }

  {
    Status status(StatusCode::kAborted, "test_message2");
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto* typed_inner_response =
        static_cast<RpcResponseImpl*>(&typed_response->inner_metadata_->mutableResponse());

    EXPECT_EQ(ResponseStatus::ServerError, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(false, typed_inner_response->responseType().has_value());
    EXPECT_EQ("test_message2", typed_inner_response->localRawMessage().value());
  }
}

TEST(DubboClientCodecTest, DubboClientCodecTest) {
  auto codec = std::make_unique<DubboCodec>();
  codec->initilize(std::make_unique<MockSerializer>());

  MockClientCodecCallbacks callbacks;
  DubboClientCodec client_codec(std::move(codec));
  client_codec.setCodecCallbacks(callbacks);

  auto raw_serializer = const_cast<MockSerializer*>(
      dynamic_cast<const MockSerializer*>(client_codec.codec_->serializer().get()));

  // Decode failure.
  {
    client_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(0);
    buffer.writeBEInt<int64_t>(0);

    EXPECT_CALL(callbacks, onDecodingFailure());
    client_codec.decode(buffer, false);
  }

  // Waiting for header.
  {
    client_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\x02', 20}));

    // No enough header bytes and do nothing.
    client_codec.decode(buffer, false);
  }

  // Waiting for data.
  {
    client_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\x02', 20}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(8);

    // No enough body bytes and do nothing.
    client_codec.decode(buffer, false);
  }

  // Decode response.
  {
    client_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\x02', 20}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(8);
    buffer.add("anything");

    auto response = std::make_unique<RpcResponseImpl>();
    response->setResponseType(RpcResponseType::ResponseWithValue);

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(Return(ByMove(std::move(response))));

    EXPECT_CALL(callbacks, onDecodingSuccess(_));
    client_codec.decode(buffer, false);
  }

  // Encode normal request.
  {
    MockEncodingCallbacks encoding_callbacks;

    DubboRequest request(createDubboRequst(false));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _));
    EXPECT_CALL(encoding_callbacks, onEncodingSuccess(_, _));

    client_codec.encode(request, encoding_callbacks);
  }

  // Encode one-way request.
  {
    MockEncodingCallbacks encoding_callbacks;

    DubboRequest request(createDubboRequst(true));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _));
    EXPECT_CALL(encoding_callbacks, onEncodingSuccess(_, _));

    client_codec.encode(request, encoding_callbacks);
  }
}

TEST(DubboCodecFactoryTest, DubboCodecFactoryTest) {
  DubboCodecFactory factory;

  EXPECT_NE(nullptr, factory.createClientCodec().get());
  EXPECT_NE(nullptr, factory.createServerCodec().get());
}

TEST(DubboCodecFactoryConfigTest, DubboCodecFactoryConfigTest) {
  DubboCodecFactoryConfig config;
  EXPECT_EQ("envoy.generic_proxy.codecs.dubbo", config.name());
  auto proto_config = config.createEmptyConfigProto();

  Server::Configuration::MockFactoryContext context;

  EXPECT_NE(nullptr, config.createCodecFactory(*proto_config, context));
}

} // namespace
} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
