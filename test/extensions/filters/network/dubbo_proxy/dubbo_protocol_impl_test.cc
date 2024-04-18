#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "source/extensions/filters/network/dubbo_proxy/protocol.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(DubboProtocolImplTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  auto result = dubbo_protocol.decodeHeader(buffer, metadata);
  EXPECT_FALSE(result.second);

  buffer.add(std::string(15, 0x00));
  result = dubbo_protocol.decodeHeader(buffer, metadata);
  EXPECT_FALSE(result.second);
}

TEST(DubboProtocolImplTest, Name) {
  DubboProtocolImpl dubbo_protocol;
  EXPECT_EQ(dubbo_protocol.name(), "dubbo");
}

TEST(DubboProtocolImplTest, Normal) {
  DubboProtocolImpl dubbo_protocol;
  dubbo_protocol.initSerializer(SerializationType::Hessian2);
  // Normal dubbo request message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);

    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    auto context = result.first;
    EXPECT_TRUE(result.second);
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, context->bodySize());
    EXPECT_EQ(MessageType::Request, metadata->messageType());
  }

  // Normal dubbo response message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x42, 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    auto context = result.first;
    EXPECT_TRUE(result.second);
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(1, context->bodySize());
    EXPECT_EQ(MessageType::Response, metadata->messageType());
  }

  // Normal dubbo response with exception.
  {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl body_buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 20}));
    addInt64(buffer, 1);

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(body_buffer));
    // Encode the fake response type. `0` means the response is an exception without attachments.
    encoder.encode<int32_t>(0);
    encoder.encode<std::string>("fake_exception");

    auto body_size = body_buffer.length();
    addInt32(buffer, body_size);
    buffer.move(body_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    auto context = result.first;
    EXPECT_TRUE(result.second);
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(body_size, context->bodySize());
    EXPECT_EQ(MessageType::Response, metadata->messageType());

    context->originMessage().move(buffer, context->headerSize());

    auto body_result = dubbo_protocol.decodeData(buffer, context, metadata);

    EXPECT_TRUE(body_result);
    EXPECT_EQ(MessageType::Exception, metadata->messageType());
  }

  // Normal dubbo response with error.
  {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl body_buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 40}));
    addInt64(buffer, 1);

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(body_buffer));
    // No response type in the body for non `20` response.
    encoder.encode<std::string>("error_string");

    auto body_size = body_buffer.length();
    addInt32(buffer, body_size);
    buffer.move(body_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    auto context = result.first;
    EXPECT_TRUE(result.second);
    EXPECT_EQ(ResponseStatus::BadRequest, metadata->responseStatus());
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(body_size, context->bodySize());
    EXPECT_EQ(MessageType::Response, metadata->messageType());

    context->originMessage().move(buffer, context->headerSize());

    auto body_result = dubbo_protocol.decodeData(buffer, context, metadata);

    EXPECT_TRUE(body_result);
    EXPECT_EQ(MessageType::Exception, metadata->messageType());
  }

  // Normal dubbo response with error of ServerError.
  {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl body_buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 80}));
    addInt64(buffer, 1);

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(body_buffer));
    // No response type in the body for non `20` response.
    encoder.encode<std::string>("error_string");

    auto body_size = body_buffer.length();
    addInt32(buffer, body_size);
    buffer.move(body_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    auto context = result.first;
    EXPECT_TRUE(result.second);
    EXPECT_EQ(ResponseStatus::ServerError, metadata->responseStatus());
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(body_size, context->bodySize());
    EXPECT_EQ(MessageType::Response, metadata->messageType());

    context->originMessage().move(buffer, context->headerSize());

    auto body_result = dubbo_protocol.decodeData(buffer, context, metadata);

    EXPECT_TRUE(body_result);
    EXPECT_EQ(MessageType::Exception, metadata->messageType());
  }
}

TEST(DubboProtocolImplTest, InvalidProtocol) {
  DubboProtocolImpl dubbo_protocol;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();

  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message magic number 0");
  }

  // Invalid message size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, DubboProtocolImpl::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboProtocolImpl::MaxBodySize + 1);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              exception_string);
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message response status 0");
  }
}

TEST(DubboProtocolImplTest, DubboProtocolConfigFactory) {
  auto protocol = NamedProtocolConfigFactory::getFactory(ProtocolType::Dubbo)
                      .createProtocol(SerializationType::Hessian2);
  EXPECT_EQ(protocol->name(), "dubbo");
  EXPECT_EQ(protocol->type(), ProtocolType::Dubbo);
  EXPECT_EQ(protocol->serializer()->type(), SerializationType::Hessian2);
}

TEST(DubboProtocolImplTest, encode) {
  MessageMetadata metadata;
  metadata.setMessageType(MessageType::Response);
  metadata.setResponseStatus(ResponseStatus::ServiceNotFound);
  metadata.setSerializationType(SerializationType::Hessian2);
  metadata.setRequestId(100);

  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  dubbo_protocol.initSerializer(SerializationType::Hessian2);
  std::string content("this is test data");
  EXPECT_TRUE(dubbo_protocol.encode(buffer, metadata, content, RpcResponseType::ResponseWithValue));

  MessageMetadataSharedPtr output_metadata = std::make_shared<MessageMetadata>();
  auto result = dubbo_protocol.decodeHeader(buffer, output_metadata);
  EXPECT_TRUE(result.second);

  EXPECT_EQ(metadata.messageType(), output_metadata->messageType());
  EXPECT_EQ(metadata.responseStatus(), output_metadata->responseStatus());
  EXPECT_EQ(metadata.serializationType(), output_metadata->serializationType());
  EXPECT_EQ(metadata.requestId(), output_metadata->requestId());

  Buffer::OwnedImpl body_buffer;
  size_t serialized_body_size = dubbo_protocol.serializer()->serializeRpcResult(
      body_buffer, content, RpcResponseType::ResponseWithValue);
  auto context = result.first;
  EXPECT_EQ(context->bodySize(), serialized_body_size);

  buffer.drain(context->headerSize());
  EXPECT_TRUE(dubbo_protocol.decodeData(buffer, context, output_metadata));
}

TEST(DubboProtocolImplTest, HeartBeatResponseTest) {
  MessageMetadata metadata;
  metadata.setMessageType(MessageType::HeartbeatResponse);
  metadata.setResponseStatus(ResponseStatus::Ok);
  metadata.setSerializationType(SerializationType::Hessian2);
  metadata.setRequestId(100);

  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  dubbo_protocol.initSerializer(SerializationType::Hessian2);
  EXPECT_TRUE(dubbo_protocol.encode(buffer, metadata, "", RpcResponseType::ResponseWithValue));
  // 16 bytes header and one byte null object body.
  EXPECT_EQ(17, buffer.length());
}

TEST(DubboProtocolImplTest, decode) {
  Buffer::OwnedImpl buffer;
  MessageMetadataSharedPtr metadata;
  DubboProtocolImpl dubbo_protocol;

  // metadata is nullptr
  EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                            "invalid metadata parameter");

  metadata = std::make_shared<MessageMetadata>();

  // Invalid message header size
  EXPECT_FALSE(dubbo_protocol.decodeHeader(buffer, metadata).second);

  // Invalid dubbo magic number
  {
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message magic number 0");
    buffer.drain(buffer.length());
  }

  // Invalid message body size
  {
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, DubboProtocolImpl::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboProtocolImpl::MaxBodySize + 1);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              exception_string);
    buffer.drain(buffer.length());
  }

  // Invalid serialization type
  {
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
    buffer.drain(buffer.length());
  }

  // Invalid response status
  {
    buffer.add(std::string({'\xda', '\xbb', 0x42, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decodeHeader(buffer, metadata), EnvoyException,
                              "invalid dubbo message response status 0");
    buffer.drain(buffer.length());
  }

  // The dubbo request message
  {
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    EXPECT_TRUE(result.second);
    auto context = result.first;
    EXPECT_EQ(1, context->bodySize());
    EXPECT_EQ(MessageType::Request, metadata->messageType());
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(SerializationType::Hessian2, metadata->serializationType());
    buffer.drain(buffer.length());
  }

  // The One-way dubbo request message
  {
    buffer.add(std::string({'\xda', '\xbb', '\x82', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    auto result = dubbo_protocol.decodeHeader(buffer, metadata);
    EXPECT_TRUE(result.second);
    auto context = result.first;
    EXPECT_EQ(1, context->bodySize());
    EXPECT_EQ(MessageType::Oneway, metadata->messageType());
    EXPECT_EQ(1, metadata->requestId());
    EXPECT_EQ(SerializationType::Hessian2, metadata->serializationType());
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
