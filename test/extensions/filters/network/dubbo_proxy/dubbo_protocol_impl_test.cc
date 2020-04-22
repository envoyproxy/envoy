#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

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
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context->body_size());
    EXPECT_EQ(MessageType::Request, metadata->message_type());
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
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context->body_size());
    EXPECT_EQ(MessageType::Response, metadata->message_type());
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

  EXPECT_EQ(metadata.message_type(), output_metadata->message_type());
  EXPECT_EQ(metadata.response_status(), output_metadata->response_status());
  EXPECT_EQ(metadata.serialization_type(), output_metadata->serialization_type());
  EXPECT_EQ(metadata.request_id(), output_metadata->request_id());

  Buffer::OwnedImpl body_buffer;
  size_t serialized_body_size = dubbo_protocol.serializer()->serializeRpcResult(
      body_buffer, content, RpcResponseType::ResponseWithValue);
  auto context = result.first;
  EXPECT_EQ(context->body_size(), serialized_body_size);
  EXPECT_EQ(false, context->hasAttachments());
  EXPECT_EQ(0, context->attachments().size());

  buffer.drain(context->header_size());
  EXPECT_TRUE(dubbo_protocol.decodeData(buffer, context, output_metadata));
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
    EXPECT_EQ(1, context->body_size());
    EXPECT_EQ(MessageType::Request, metadata->message_type());
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(SerializationType::Hessian2, metadata->serialization_type());
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
    EXPECT_EQ(1, context->body_size());
    EXPECT_EQ(MessageType::Oneway, metadata->message_type());
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(SerializationType::Hessian2, metadata->serialization_type());
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
