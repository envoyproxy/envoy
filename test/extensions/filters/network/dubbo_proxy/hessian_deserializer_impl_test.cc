#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NotNull;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(HessianProtocolTest, Name) {
  HessianDeserializerImpl deserializer;
  EXPECT_EQ(deserializer.name(), "hessian");
}

TEST(HessianProtocolTest, deserializeRpcInvocation) {
  HessianDeserializerImpl deserializer;

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    deserializer.deserializeRpcInvocation(buffer, buffer.length(), metadata);
    EXPECT_STREQ("test", metadata->method_name().value().c_str());
    EXPECT_STREQ("test", metadata->service_name().c_str());
    EXPECT_STREQ("0.0.0", metadata->service_version().value().c_str());
  }

  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    std::string exception_string = fmt::format("RpcInvocation size({}) large than body size({})",
                                               buffer.length(), buffer.length() - 1);
    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    EXPECT_THROW_WITH_MESSAGE(
        deserializer.deserializeRpcInvocation(buffer, buffer.length() - 1, metadata),
        EnvoyException, exception_string);
  }
}

TEST(HessianProtocolTest, deserializeRpcResult) {
  HessianDeserializerImpl deserializer;

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    auto result = deserializer.deserializeRpcResult(buffer, 4);
    EXPECT_FALSE(result->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x93',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    auto result = deserializer.deserializeRpcResult(buffer, 4);
    EXPECT_TRUE(result->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x90',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    auto result = deserializer.deserializeRpcResult(buffer, 4);
    EXPECT_TRUE(result->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x91',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    auto result = deserializer.deserializeRpcResult(buffer, 4);
    EXPECT_TRUE(result->hasException());
  }

  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x05, 't', 'e', 's', 't', // return body
    }));
    EXPECT_THROW_WITH_MESSAGE(deserializer.deserializeRpcResult(buffer, 0), EnvoyException,
                              "RpcResult size(1) large than body size(0)");
  }

  // incorrect return type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x96',                   // incorrect return type
        0x05, 't', 'e', 's', 't', // return body
    }));
    EXPECT_THROW_WITH_MESSAGE(deserializer.deserializeRpcResult(buffer, buffer.length()),
                              EnvoyException, "not supported return type 6");
  }

  // incorrect value size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x92',                   // without the value of the return type
        0x05, 't', 'e', 's', 't', // return body
    }));
    std::string exception_string =
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    buffer.length() - 1);
    EXPECT_THROW_WITH_MESSAGE(deserializer.deserializeRpcResult(buffer, buffer.length()),
                              EnvoyException, exception_string);
  }
}

TEST(HessianProtocolTest, HessianDeserializerConfigFactory) {
  auto deserializer =
      NamedDeserializerConfigFactory::getFactory(SerializationType::Hessian).createDeserializer();
  EXPECT_EQ(deserializer->name(), "hessian");
  EXPECT_EQ(deserializer->type(), SerializationType::Hessian);
}

TEST(HessianProtocolTest, serializeRpcResult) {
  Buffer::OwnedImpl buffer;
  std::string mock_response("invalid method name 'Add'");
  RpcResponseType mock_response_type = RpcResponseType::ResponseWithException;
  HessianDeserializerImpl deserializer;

  deserializer.serializeRpcResult(buffer, mock_response, mock_response_type);

  size_t hessian_int_size;
  int type_value = HessianUtils::peekInt(buffer, &hessian_int_size);
  EXPECT_EQ(static_cast<uint8_t>(mock_response_type), static_cast<uint8_t>(type_value));

  size_t hessian_string_size;
  std::string content = HessianUtils::peekString(buffer, &hessian_string_size, sizeof(uint8_t));
  EXPECT_EQ(mock_response, content);

  EXPECT_EQ(buffer.length(), hessian_int_size + hessian_string_size);

  size_t body_size = mock_response.size() + sizeof(mock_response_type);
  auto result = deserializer.deserializeRpcResult(buffer, body_size);
  EXPECT_TRUE(result->hasException());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy