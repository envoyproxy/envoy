#include "source/extensions/common/dubbo/codec.h"

#include "test/extensions/common/dubbo/mocks.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include <memory>
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

inline void addInt32(Buffer::Instance& buffer, uint32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt64(Buffer::Instance& buffer, uint64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
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
    buffer.add(std::string({'\xda', '\xbb', '\xa2', 0x00}));
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

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
