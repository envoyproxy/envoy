#include <memory>

#include "source/extensions/common/dubbo/codec.h"
#include "source/extensions/common/dubbo/message_impl.h"

#include "test/extensions/common/dubbo/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

constexpr uint16_t MagicNumber = 0xdabb;
constexpr uint64_t FlagOffset = 2;
constexpr uint64_t StatusOffset = 3;
constexpr uint64_t RequestIDOffset = 4;
constexpr uint64_t BodySizeOffset = 12;

inline void addInt32(Buffer::Instance& buffer, uint32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt64(Buffer::Instance& buffer, uint64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
}

TEST(DubboCodecTest, GetSerializer) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  EXPECT_EQ(raw_serializer, codec.serializer().get());
}

TEST(DubboCodecTest, CodecWithSerializer) {
  auto codec = DubboCodec::codecFromSerializeType(SerializeType::Hessian2);
  EXPECT_EQ(SerializeType::Hessian2, codec->serializer()->type());
}

TEST(DubboCodecTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  DubboCodec codec;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  auto result = codec.decodeHeader(buffer, *metadata);
  EXPECT_EQ(DecodeStatus::Waiting, result);

  buffer.add(std::string(15, 0x00));
  result = codec.decodeHeader(buffer, *metadata);
  EXPECT_EQ(DecodeStatus::Waiting, result);
}

TEST(DubboCodecTest, DecodeHeaderTest) {
  DubboCodec codec;
  auto serializer = std::make_unique<MockSerializer>();
  codec.initilize(std::move(serializer));

  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(codec.decodeHeader(buffer, *metadata), EnvoyException,
                              "invalid dubbo message magic number 0");
  }

  // Invalid message body size
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, DubboCodec::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboCodec::MaxBodySize + 1);
    EXPECT_THROW_WITH_MESSAGE(codec.decodeHeader(buffer, *metadata), EnvoyException,
                              exception_string);
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(codec.decodeHeader(buffer, *metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x02, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(codec.decodeHeader(buffer, *metadata), EnvoyException,
                              "invalid dubbo message response status 0");
  }

  // Normal dubbo request message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::Request, metadata->messageType());
  }

  // Oneway dubbo request message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\x82', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::Oneway, metadata->messageType());
  }

  // Heartbeat request message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xe2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::HeartbeatRequest, metadata->messageType());
  }

  // Normal dubbo response message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x02, 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::Response, metadata->messageType());
    EXPECT_EQ(true, metadata->hasResponseStatus());
    EXPECT_EQ(ResponseStatus::Ok, metadata->responseStatus());
  }

  // Normal dubbo response with error.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x02, 40}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::Exception, metadata->messageType());
    EXPECT_EQ(true, metadata->hasResponseStatus());
    EXPECT_EQ(ResponseStatus::BadRequest, metadata->responseStatus());
  }

  // Heartbeat response message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\x22', 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = codec.decodeHeader(buffer, *metadata);
    EXPECT_EQ(DecodeStatus::Success, result);

    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, metadata->context().bodySize());
    EXPECT_EQ(MessageType::HeartbeatResponse, metadata->messageType());
  }
}

TEST(DubboCodecTest, DecodeDataTest) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  // No enough data.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(MessageType::Request);
    context->setRequestId(1);
    context->setBodySize(buffer.length() + 1);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_EQ(DecodeStatus::Waiting, codec.decodeData(buffer, metadata));
  }

  // Decode request body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(MessageType::Request);
    context->setRequestId(1);
    context->setBodySize(buffer.length());
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _))
        .WillOnce(testing::Return(testing::ByMove(std::make_unique<RpcRequestImpl>())));

    EXPECT_EQ(DecodeStatus::Success, codec.decodeData(buffer, metadata));
    EXPECT_EQ(true, metadata.hasRequest());
  }

  // Decode request body with null request.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(MessageType::HeartbeatRequest);
    context->setRequestId(1);
    context->setBodySize(buffer.length());
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, deserializeRpcRequest(_, _))
        .WillOnce(testing::Return(testing::ByMove(nullptr)));

    EXPECT_EQ(DecodeStatus::Success, codec.decodeData(buffer, metadata));
    EXPECT_EQ(false, metadata.hasRequest());
  }

  // Decode response body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(MessageType::Response);
    context->setRequestId(1);
    context->setBodySize(buffer.length());
    context->setResponseStatus(ResponseStatus::Ok);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(testing::Return(testing::ByMove(std::make_unique<RpcResponseImpl>())));

    EXPECT_EQ(DecodeStatus::Success, codec.decodeData(buffer, metadata));
    EXPECT_EQ(true, metadata.hasResponse());
  }

  // Decode response body.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(MessageType::HeartbeatResponse);
    context->setRequestId(1);
    context->setBodySize(buffer.length());
    context->setResponseStatus(ResponseStatus::Ok);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, deserializeRpcResponse(_, _))
        .WillOnce(testing::Return(testing::ByMove(nullptr)));

    EXPECT_EQ(DecodeStatus::Success, codec.decodeData(buffer, metadata));
    EXPECT_EQ(false, metadata.hasResponse());
  }

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything");

    MessageMetadata metadata;
    auto context = std::make_unique<Context>();

    context->setMessageType(static_cast<MessageType>(6));
    context->setRequestId(1);
    context->setBodySize(buffer.length());
    context->setResponseStatus(ResponseStatus::Ok);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_DEATH(codec.decodeData(buffer, metadata), ".*panic: corrupted enum.*");
  }
}

TEST(DubboCodecTest, EncodeTest) {
  DubboCodec codec;

  auto serializer = std::make_unique<MockSerializer>();
  // Keep this for mock.
  auto raw_serializer = serializer.get();
  codec.initilize(std::move(serializer));

  // Encode normal request.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _))
        .WillOnce(testing::Invoke(
            [](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("anything"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag. 0x80 for request, 0x40 for two way, 0x20 for event/heartbeat, 0x02 for hessian2
    // serialize type. So, the final flag of normal two way request is 0xc2.
    EXPECT_EQ(0xc2, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode oneway request.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Oneway);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _))
        .WillOnce(testing::Invoke(
            [](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("anything"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x82, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode heartbeat request.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::HeartbeatRequest);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcRequest(_, _))
        .WillOnce(
            testing::Invoke([](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("N"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 1, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0xe2, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(0, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(1, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode normal response.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _))
        .WillOnce(testing::Invoke(
            [](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("anything"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x02, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(20, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode exception response
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Exception);
    context->setResponseStatus(ResponseStatus::BadRequest);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _))
        .WillOnce(testing::Invoke(
            [](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("anything"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 8, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x02, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(40, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(8, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode heartbeat response
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::HeartbeatResponse);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_CALL(*raw_serializer, serializeRpcResponse(_, _))
        .WillOnce(
            testing::Invoke([](Buffer::Instance& buffer, MessageMetadata&) { buffer.add("N"); }));

    codec.encode(buffer, metadata);

    EXPECT_EQ(DubboCodec::HeadersSize + 1, buffer.length());

    // Check magic number.
    EXPECT_EQ(MagicNumber, buffer.peekBEInt<uint16_t>());

    // Check flag.
    EXPECT_EQ(0x22, buffer.peekBEInt<uint8_t>(FlagOffset));

    // Check status. Only response has valid status byte.
    EXPECT_EQ(20, buffer.peekBEInt<uint8_t>(StatusOffset));

    // Check request id.
    EXPECT_EQ(12345, buffer.peekBEInt<int64_t>(RequestIDOffset));

    // Check body size.
    EXPECT_EQ(1, buffer.peekBEInt<int32_t>(BodySizeOffset));
  }

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    auto context = std::make_unique<Context>();
    context->setMessageType(static_cast<MessageType>(6));
    context->setResponseStatus(ResponseStatus::Ok);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    EXPECT_DEATH(codec.encode(buffer, metadata), ".*panic: corrupted enum.*");
  }
}

TEST(DubboCodecTest, EncodeHeaderForTestTest) {
  DubboCodec codec;

  // Encode unexpected message type will cause exit.
  {
    Buffer::OwnedImpl buffer;

    auto context = std::make_unique<Context>();
    context->setMessageType(static_cast<MessageType>(6));
    context->setResponseStatus(ResponseStatus::Ok);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    EXPECT_DEATH(codec.encodeHeaderForTest(buffer, *context), ".*panic: corrupted enum.*");
  }
}

TEST(DirectResponseUtilTest, DirectResponseUtilTest) {
  // Heartbeat response test.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::HeartbeatRequest);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response = DirectResponseUtil::heartbeatResponse(metadata);

    EXPECT_EQ(MessageType::HeartbeatResponse, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::Ok, response->responseStatus());
    EXPECT_EQ(true, response->context().heartbeat());
  }

  // Local normal response.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response = DirectResponseUtil::localResponse(
        metadata, ResponseStatus::Ok, RpcResponseType::ResponseWithValue, "anything");

    EXPECT_EQ(MessageType::Response, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::Ok, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    EXPECT_EQ(RpcResponseType::ResponseWithValue, response->response().responseType().value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }

  // Local normal response without response type.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response =
        DirectResponseUtil::localResponse(metadata, ResponseStatus::Ok, absl::nullopt, "anything");

    EXPECT_EQ(MessageType::Response, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::Ok, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    EXPECT_EQ(false, response->response().responseType().has_value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }

  // Local normal response with exception type.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response = DirectResponseUtil::localResponse(
        metadata, ResponseStatus::Ok, RpcResponseType::ResponseWithException, "anything");

    EXPECT_EQ(MessageType::Exception, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::Ok, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    EXPECT_EQ(RpcResponseType::ResponseWithException, response->response().responseType().value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }

  // Local normal response with exception type.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response = DirectResponseUtil::localResponse(
        metadata, ResponseStatus::Ok, RpcResponseType::ResponseWithExceptionWithAttachments,
        "anything");

    EXPECT_EQ(MessageType::Exception, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::Ok, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    EXPECT_EQ(RpcResponseType::ResponseWithExceptionWithAttachments,
              response->response().responseType().value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }

  // Local exception response.
  {
    MessageMetadata metadata;
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);
    context->setRequestId(12345);
    context->setSerializeType(SerializeType::Hessian2);

    metadata.setContext(std::move(context));

    auto response = DirectResponseUtil::localResponse(
        metadata, ResponseStatus::BadRequest, RpcResponseType::ResponseWithValue, "anything");

    EXPECT_EQ(MessageType::Exception, response->messageType());
    EXPECT_EQ(12345, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::BadRequest, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    // Response type will be ignored for non-Ok response.
    EXPECT_EQ(false, response->response().responseType().has_value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }

  // Local response without request context.
  {
    MessageMetadata metadata;

    auto response = DirectResponseUtil::localResponse(
        metadata, ResponseStatus::BadRequest, RpcResponseType::ResponseWithValue, "anything");

    EXPECT_EQ(MessageType::Exception, response->messageType());
    EXPECT_EQ(0, response->requestId());
    EXPECT_EQ(SerializeType::Hessian2, response->context().serializeType());
    EXPECT_EQ(ResponseStatus::BadRequest, response->responseStatus());
    EXPECT_EQ(false, response->context().heartbeat());
    // Response type will be ignored for non-Ok response.
    EXPECT_EQ(false, response->response().responseType().has_value());

    auto typed_response = dynamic_cast<RpcResponseImpl*>(&response->mutableResponse());
    EXPECT_NE(nullptr, typed_response);
    EXPECT_EQ("anything", typed_response->localRawMessage().value());
  }
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
