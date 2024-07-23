#include <cstdint>
#include <memory>

#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/filters/network/generic_proxy/codecs/dubbo/config.h"

#include "test/extensions/common/dubbo/mocks.h"
#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/mocks/server/factory_context.h"

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
  auto request = std::make_unique<RpcRequest>("fake_dubbo_version", "fake_service", "fake_version",
                                              "fake_method");

  request->content().initialize("", {}, {});
  request->content().setAttachment("group", "fake_group");

  auto context = std::make_unique<Context>();
  context->setMessageType(one_way_request ? MessageType::Oneway : MessageType::Request);
  context->setRequestId(123456);

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

  {
    auto frame_flags = request.frameFlags();
    EXPECT_EQ(frame_flags.streamId(), 123456);
    EXPECT_EQ(frame_flags.endStream(), true);
    EXPECT_EQ(frame_flags.heartbeat(), false);
    EXPECT_EQ(frame_flags.oneWayStream(), false);
    EXPECT_EQ(frame_flags.drainClose(), false);
  }

  // Static attributes test.
  { EXPECT_EQ("dubbo", request.protocol()); }

  // Basic attributes test.
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
  // Iterate headers and break;
  {
    size_t attachment_size = 0;
    request.forEach([&attachment_size](absl::string_view, absl::string_view) {
      attachment_size++;
      return false;
    });
    // Version is not part of attachments. So there are only 2 attachments.
    EXPECT_EQ(1, attachment_size);
  }

  // Erase headers.
  {
    request.erase("group");
    EXPECT_EQ(false, request.get("group").has_value());

    request.erase("custom_key");
    EXPECT_EQ(false, request.get("custom_key").has_value());
  }
}

TEST(DubboRequestTest, OneWayDubboRequestTest) {
  DubboRequest request(createDubboRequst(true));

  {
    auto frame_flags = request.frameFlags();
    EXPECT_EQ(frame_flags.streamId(), 123456);
    EXPECT_EQ(frame_flags.endStream(), true);
    EXPECT_EQ(frame_flags.heartbeat(), false);
    EXPECT_EQ(frame_flags.oneWayStream(), true);
    EXPECT_EQ(frame_flags.drainClose(), false);
  }
}

TEST(DubboResponseTest, DubboResponseTest) {
  DubboRequest request(createDubboRequst(false));

  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));
    auto frame_flags = response.frameFlags();
    EXPECT_EQ(frame_flags.streamId(), 123456);
    EXPECT_EQ(frame_flags.endStream(), true);
    EXPECT_EQ(frame_flags.heartbeat(), false);
    EXPECT_EQ(frame_flags.oneWayStream(), false);
    EXPECT_EQ(frame_flags.drainClose(), false);
  }

  // Static attributes test.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));
    EXPECT_EQ("dubbo", response.protocol());
  }

  // Response status check.
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));
    EXPECT_EQ(20, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithException));
    EXPECT_EQ(20, response.status().code());
  }
  {
    DubboResponse response(createDubboResponse(
        request, ResponseStatus::Ok, RpcResponseType::ResponseWithExceptionWithAttachments));
    EXPECT_EQ(20, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ClientTimeout, absl::nullopt));
    EXPECT_EQ(30, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServerTimeout, absl::nullopt));
    EXPECT_EQ(31, response.status().code());
  }
  {
    DubboResponse response(createDubboResponse(request, ResponseStatus::BadRequest, absl::nullopt));
    EXPECT_EQ(40, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::BadResponse, absl::nullopt));
    EXPECT_EQ(50, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServiceNotFound, absl::nullopt));
    EXPECT_EQ(60, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServiceError, absl::nullopt));
    EXPECT_EQ(70, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ServerError, absl::nullopt));
    EXPECT_EQ(80, response.status().code());
  }
  {
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::ClientError, absl::nullopt));
    EXPECT_EQ(90, response.status().code());
  }
  {
    DubboResponse response(createDubboResponse(
        request, ResponseStatus::ServerThreadpoolExhaustedError, absl::nullopt));
    EXPECT_EQ(100, response.status().code());
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

    EXPECT_CALL(callbacks, onDecodingFailure(_));
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
        .WillOnce(Return(ByMove(std::make_unique<RpcRequest>("a", "b", "c", "d"))));

    EXPECT_CALL(callbacks, onDecodingSuccess(_, _));
    server_codec.decode(buffer, false);
  }

  // Decode heartbeat request.
  {
    server_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xe2', 00}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(1);
    buffer.writeByte('N');

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _));
    EXPECT_CALL(callbacks, writeToConnection(_));
    server_codec.decode(buffer, false);
  }

  // Encode response.
  {

    MockEncodingContext encoding_context;
    DubboRequest request(createDubboRequst(false));
    DubboResponse response(
        createDubboResponse(request, ResponseStatus::Ok, RpcResponseType::ResponseWithValue));

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _));
    EXPECT_CALL(callbacks, writeToConnection(_));

    EXPECT_TRUE(server_codec.encode(response, encoding_context).ok());
  }

  {
    Status status = absl::OkStatus();
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "anything", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto& typed_inner_response = typed_response->inner_metadata_->mutableResponse();

    EXPECT_EQ(ResponseStatus::Ok, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(RpcResponseType::ResponseWithException, typed_inner_response.responseType().value());
    EXPECT_EQ("anything", typed_inner_response.content().result()->toString().value().get());
    EXPECT_EQ("envoy_response", typed_inner_response.content().attachments().at("reason"));
  }

  {
    Status status(StatusCode::kInvalidArgument, "test_message");
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "anything", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto& typed_inner_response = typed_response->inner_metadata_->mutableResponse();

    EXPECT_EQ(ResponseStatus::BadRequest, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(false, typed_inner_response.responseType().has_value());

    EXPECT_EQ("anything", typed_inner_response.content().result()->toString().value().get());
    EXPECT_EQ("test_message", typed_inner_response.content().attachments().at("reason"));
  }

  {
    Status status(StatusCode::kAborted, "test_message2");
    DubboRequest request(createDubboRequst(false));

    auto response = server_codec.respond(status, "anything", request);
    auto* typed_response = static_cast<DubboResponse*>(response.get());
    auto& typed_inner_response = typed_response->inner_metadata_->mutableResponse();

    EXPECT_EQ(ResponseStatus::ServerError, typed_response->inner_metadata_->responseStatus());
    EXPECT_EQ(false, typed_inner_response.responseType().has_value());

    EXPECT_EQ("anything", typed_inner_response.content().result()->toString().value().get());
    EXPECT_EQ("test_message2", typed_inner_response.content().attachments().at("reason"));
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

    EXPECT_CALL(callbacks, onDecodingFailure(_));
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

    auto response = std::make_unique<RpcResponse>();
    response->setResponseType(RpcResponseType::ResponseWithValue);

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(Return(ByMove(std::move(response))));

    EXPECT_CALL(callbacks, onDecodingSuccess(_, _));
    client_codec.decode(buffer, false);
  }

  // Decode heartbeat request.
  {
    client_codec.metadata_.reset();

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xe2', 00}));
    buffer.writeBEInt<int64_t>(1);
    buffer.writeBEInt<int32_t>(1);
    buffer.writeByte('N');

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _));
    EXPECT_CALL(callbacks, writeToConnection(_));
    client_codec.decode(buffer, false);
  }

  // Encode normal request.
  {
    MockEncodingContext encoding_context;

    DubboRequest request(createDubboRequst(false));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _));
    EXPECT_CALL(callbacks, writeToConnection(_));

    EXPECT_TRUE(client_codec.encode(request, encoding_context).ok());
  }

  // Encode one-way request.
  {
    MockEncodingContext encoding_context;

    DubboRequest request(createDubboRequst(true));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _));
    EXPECT_CALL(callbacks, writeToConnection(_));

    EXPECT_TRUE(client_codec.encode(request, encoding_context).ok());
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

  EXPECT_NE(nullptr, config.createCodecFactory(*proto_config, context.server_factory_context_));
}

} // namespace
} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
