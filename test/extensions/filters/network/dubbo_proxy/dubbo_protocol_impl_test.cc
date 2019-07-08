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

using testing::StrictMock;

TEST(DubboProtocolImplTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  Protocol::Context context;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context, metadata));
  buffer.add(std::string(15, 0x00));
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context, metadata));
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
    Protocol::Context context;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context.body_size_);
    EXPECT_EQ(false, context.is_heartbeat_);
    EXPECT_EQ(MessageType::Request, metadata->message_type());
  }

  // Normal dubbo response message
  {
    Buffer::OwnedImpl buffer;
    Protocol::Context context;
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    buffer.add(std::string({'\xda', '\xbb', 0x42, 20}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(1, context.body_size_);
    EXPECT_EQ(false, context.is_heartbeat_);
    EXPECT_EQ(MessageType::Response, metadata->message_type());
  }
}

TEST(DubboProtocolImplTest, InvalidProtocol) {
  DubboProtocolImpl dubbo_protocol;
  Protocol::Context context;
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();

  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
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
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              exception_string);
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xda', '\xbb', 0x42, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message response status 0");
  }
}

TEST(DubboProtocolImplTest, DubboProtocolConfigFactory) {
  auto protocol = NamedProtocolConfigFactory::getFactory(ProtocolType::Dubbo).createProtocol();
  EXPECT_EQ(protocol->name(), "dubbo");
  EXPECT_EQ(protocol->type(), ProtocolType::Dubbo);
}

TEST(DubboProtocolImplTest, encode) {
  MessageMetadata metadata;
  metadata.setMessageType(MessageType::Response);
  metadata.setResponseStatus(ResponseStatus::ServiceNotFound);
  metadata.setSerializationType(SerializationType::Hessian);
  metadata.setRequestId(100);

  Buffer::OwnedImpl buffer;
  DubboProtocolImpl dubbo_protocol;
  int32_t expect_body_size = 100;
  EXPECT_TRUE(dubbo_protocol.encode(buffer, expect_body_size, metadata));

  Protocol::Context context;
  MessageMetadataSharedPtr output_metadata = std::make_shared<MessageMetadata>();
  EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, output_metadata));

  EXPECT_EQ(metadata.message_type(), output_metadata->message_type());
  EXPECT_EQ(metadata.response_status().value(), output_metadata->response_status().value());
  EXPECT_EQ(metadata.serialization_type(), output_metadata->serialization_type());
  EXPECT_EQ(metadata.request_id(), output_metadata->request_id());
  EXPECT_EQ(context.body_size_, expect_body_size);
}

TEST(DubboProtocolImplTest, decode) {
  Buffer::OwnedImpl buffer;
  MessageMetadataSharedPtr metadata;
  Protocol::Context context;
  DubboProtocolImpl dubbo_protocol;

  // metadata is nullptr
  EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                            "invalid metadata parameter");

  metadata = std::make_shared<MessageMetadata>();

  // Invalid message header size
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context, metadata));

  // Invalid dubbo magic number
  {
    addInt64(buffer, 0);
    addInt64(buffer, 0);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
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
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              exception_string);
    buffer.drain(buffer.length());
  }

  // Invalid serialization type
  {
    buffer.add(std::string({'\xda', '\xbb', '\xc3', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message serialization type 3");
    buffer.drain(buffer.length());
  }

  // Invalid response status
  {
    buffer.add(std::string({'\xda', '\xbb', 0x42, 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context, metadata), EnvoyException,
                              "invalid dubbo message response status 0");
    buffer.drain(buffer.length());
  }

  // The dubbo request message
  {
    Protocol::Context context;
    buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, context.body_size_);
    EXPECT_FALSE(context.is_heartbeat_);
    EXPECT_EQ(MessageType::Request, metadata->message_type());
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(SerializationType::Hessian, metadata->serialization_type());
    buffer.drain(buffer.length());
  }

  // The One-way dubbo request message
  {
    Protocol::Context context;
    buffer.add(std::string({'\xda', '\xbb', '\x82', 0x00}));
    addInt64(buffer, 1);
    addInt32(buffer, 1);
    EXPECT_TRUE(dubbo_protocol.decode(buffer, &context, metadata));
    EXPECT_EQ(1, context.body_size_);
    EXPECT_FALSE(context.is_heartbeat_);
    EXPECT_EQ(MessageType::Oneway, metadata->message_type());
    EXPECT_EQ(1, metadata->request_id());
    EXPECT_EQ(SerializationType::Hessian, metadata->serialization_type());
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy