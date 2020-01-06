#include "extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "extensions/filters/network/dubbo_proxy/message_impl.h"

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

TEST(HessianProtocolTest, Name) {
  DubboHessian2SerializerImpl serializer;
  EXPECT_EQ(serializer.name(), "dubbo.hessian2");
}

TEST(HessianProtocolTest, deserializeRpcInvocation) {
  DubboHessian2SerializerImpl serializer;

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
    context->set_body_size(buffer.length());
    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_TRUE(result.second);

    auto invo = result.first;
    EXPECT_STREQ("test", invo->method_name().c_str());
    EXPECT_STREQ("test", invo->service_name().c_str());
    EXPECT_STREQ("0.0.0", invo->service_version().value().c_str());
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
    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
    context->set_body_size(buffer.length() - 1);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcInvocation(buffer, context), EnvoyException,
                              exception_string);
  }
}

TEST(HessianProtocolTest, deserializeRpcResult) {
  DubboHessian2SerializerImpl serializer;
  std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(4);
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_FALSE(result.first->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x93',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(4);
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_TRUE(result.first->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x90',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(4);
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_TRUE(result.first->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x91',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(4);
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_TRUE(result.first->hasException());
  }

  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x05, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(0);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              "RpcResult size(1) large than body size(0)");
  }

  // incorrect return type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x96',                   // incorrect return type
        0x05, 't', 'e', 's', 't', // return body
    }));
    context->set_body_size(buffer.length());
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              "not supported return type 6");
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
    context->set_body_size(buffer.length());
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              exception_string);
  }
}

TEST(HessianProtocolTest, HessianDeserializerConfigFactory) {
  auto serializer =
      NamedSerializerConfigFactory::getFactory(ProtocolType::Dubbo, SerializationType::Hessian2)
          .createSerializer();
  EXPECT_EQ(serializer->name(), "dubbo.hessian2");
  EXPECT_EQ(serializer->type(), SerializationType::Hessian2);
}

TEST(HessianProtocolTest, serializeRpcResult) {
  Buffer::OwnedImpl buffer;
  std::string mock_response("invalid method name 'Add'");
  RpcResponseType mock_response_type = RpcResponseType::ResponseWithException;
  DubboHessian2SerializerImpl serializer;

  EXPECT_NE(serializer.serializeRpcResult(buffer, mock_response, mock_response_type), 0);

  size_t hessian_int_size;
  int type_value = HessianUtils::peekInt(buffer, &hessian_int_size);
  EXPECT_EQ(static_cast<uint8_t>(mock_response_type), static_cast<uint8_t>(type_value));

  size_t hessian_string_size;
  std::string content = HessianUtils::peekString(buffer, &hessian_string_size, sizeof(uint8_t));
  EXPECT_EQ(mock_response, content);

  EXPECT_EQ(buffer.length(), hessian_int_size + hessian_string_size);

  size_t body_size = mock_response.size() + sizeof(mock_response_type);
  std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
  context->set_body_size(body_size);
  auto result = serializer.deserializeRpcResult(buffer, context);
  EXPECT_TRUE(result.first->hasException());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
