#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"

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
    context->setBodySize(buffer.length());
    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_TRUE(result.second);

    auto invo = result.first;
    EXPECT_STREQ("test", invo->methodName().c_str());
    EXPECT_STREQ("test", invo->serviceName().c_str());
    EXPECT_STREQ("0.0.0", invo->serviceVersion().value().c_str());
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
    std::string exception_string = fmt::format("RpcInvocation size({}) larger than body size({})",
                                               buffer.length(), buffer.length() - 1);
    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
    context->setBodySize(buffer.length() - 1);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcInvocation(buffer, context), EnvoyException,
                              exception_string);
  }

  // Missing key metadata.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
    }));
    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
    context->setBodySize(buffer.length());
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcInvocation(buffer, context), EnvoyException,
                              "RpcInvocation has no request metadata");
  }
}

TEST(HessianProtocolTest, deserializeRpcInvocationWithParametersOrAttachment) {
  RpcInvocationImpl::Attachment attach(std::make_unique<RpcInvocationImpl::Attachment::Map>(), 0);
  attach.insert("test1", "test_value1");
  attach.insert("test2", "test_value2");
  attach.insert("test3", "test_value3");

  RpcInvocationImpl::Parameters params;

  params.push_back(std::make_unique<Hessian2::StringObject>("test_string"));

  std::vector<uint8_t> test_binary{0, 1, 2, 3, 4};
  params.push_back(std::make_unique<Hessian2::BinaryObject>(test_binary));

  params.push_back(std::make_unique<Hessian2::LongObject>(233333));

  // 4 parameters. We will encode attachment as a map type parameter.
  std::string parameters_type = "Ljava.lang.String;[BJLjava.util.Map;";

  {
    DubboHessian2SerializerImpl serializer;
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(attach.attachment());

    size_t expected_attachment_offset = buffer.length();

    // Encode attachment
    encoder.encode<Hessian2::Object>(attach.attachment());

    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_EQ(true, result.second);

    auto invo = dynamic_cast<RpcInvocationImpl*>(result.first.get());

    context->originMessage().move(buffer, buffer.length());

    EXPECT_EQ(false, invo->hasAttachment());
    EXPECT_EQ(false, invo->hasParameters());

    auto& result_params = invo->mutableParameters();

    // When parsing parameters, attachment will not be parsed.
    EXPECT_EQ(false, invo->hasAttachment());
    EXPECT_EQ(true, invo->hasParameters());

    EXPECT_EQ(4, result_params->size());

    EXPECT_EQ("test_string", result_params->at(0)->toString().value().get());
    EXPECT_EQ(4, result_params->at(1)->toBinary().value().get().at(4));
    EXPECT_EQ(233333, *result_params->at(2)->toLong());
    EXPECT_EQ(3, result_params->at(3)->toUntypedMap().value().get().size());
    EXPECT_EQ("test_value2", result_params->at(3)
                                 ->toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());

    auto& result_attach = invo->mutableAttachment();
    EXPECT_EQ("test_value2", result_attach->attachment()
                                 .toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());

    EXPECT_EQ(expected_attachment_offset, result_attach->attachmentOffset());
  }
  {
    DubboHessian2SerializerImpl serializer;
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(attach.attachment());

    // Encode attachment
    encoder.encode<Hessian2::Object>(attach.attachment());

    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_EQ(true, result.second);

    auto invo = dynamic_cast<RpcInvocationImpl*>(result.first.get());

    context->originMessage().move(buffer, buffer.length());

    EXPECT_EQ(false, invo->hasAttachment());
    EXPECT_EQ(false, invo->hasParameters());

    auto& result_attach = invo->mutableAttachment();

    // When parsing attachment, parameters will also be parsed.
    EXPECT_EQ(true, invo->hasAttachment());
    EXPECT_EQ(true, invo->hasParameters());

    EXPECT_EQ("test_value2", result_attach->attachment()
                                 .toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());

    auto& result_params = invo->parameters();
    EXPECT_EQ("test_value2", result_params.at(3)
                                 ->toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());
  }
  // Test case that request only have parameters.
  {
    DubboHessian2SerializerImpl serializer;
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(attach.attachment());

    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_EQ(true, result.second);

    auto invo = dynamic_cast<RpcInvocationImpl*>(result.first.get());

    context->originMessage().move(buffer, buffer.length());

    EXPECT_EQ(false, invo->hasAttachment());
    EXPECT_EQ(false, invo->hasParameters());

    auto& result_attach = invo->mutableAttachment();

    // When parsing attachment, parameters will also be parsed.
    EXPECT_EQ(true, invo->hasAttachment());
    EXPECT_EQ(true, invo->hasParameters());

    auto& result_params = invo->parameters();
    EXPECT_EQ("test_value2", result_params.at(3)
                                 ->toUntypedMap()
                                 .value()
                                 .get()
                                 .find("test2")
                                 ->second->toString()
                                 .value()
                                 .get());

    EXPECT_EQ(true, result_attach->attachment().toUntypedMap().value().get().empty());
  }
  // Test the case where there are not enough parameters in the request buffer.
  {
    DubboHessian2SerializerImpl serializer;
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    // There are actually only three parameters in the request.
    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }

    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_EQ(true, result.second);

    auto invo = dynamic_cast<RpcInvocationImpl*>(result.first.get());

    context->originMessage().move(buffer, buffer.length());

    // There are not enough parameters and throws an exception.
    EXPECT_THROW_WITH_MESSAGE(invo->mutableParameters(), EnvoyException,
                              "Cannot parse RpcInvocation parameter from buffer");
  }
  // Test for incorrect attachment types.
  {
    DubboHessian2SerializerImpl serializer;
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));

    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    encoder.encode<std::string>(parameters_type);

    for (const auto& param : params) {
      encoder.encode<Hessian2::Object>(*param);
    }
    // Encode an untyped map object as fourth parameter.
    encoder.encode<Hessian2::Object>(attach.attachment());

    // Encode a string object as attachment.
    encoder.encode<Hessian2::Object>(*params[0]);

    std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcInvocation(buffer, context);
    EXPECT_EQ(true, result.second);

    auto invo = dynamic_cast<RpcInvocationImpl*>(result.first.get());

    context->originMessage().move(buffer, buffer.length());

    auto& result_attach = invo->mutableAttachment();
    EXPECT_EQ(true, result_attach->attachment().toUntypedMap().value().get().empty());
  }
}

TEST(HessianProtocolTest, deserializeRpcResult) {
  DubboHessian2SerializerImpl serializer;
  std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x04,
        't',
        'e',
        's',
        't',
    }));

    context->setBodySize(buffer.length());

    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              "Cannot parse RpcResult type from buffer");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->setBodySize(buffer.length());
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
    context->setBodySize(buffer.length());
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
    context->setBodySize(4);
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
    context->setBodySize(4);
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_FALSE(result.first->hasException());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x95', // return type
        'H',    // return attachment
        0x03,
        'k',
        'e',
        'y',
        0x05,
        'v',
        'a',
        'l',
        'u',
        'e',
        'Z',
    }));

    context->setBodySize(buffer.length());
    auto result = serializer.deserializeRpcResult(buffer, context);
    EXPECT_TRUE(result.second);
    EXPECT_FALSE(result.first->hasException());
  }

  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->setBodySize(0);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              "RpcResult size(1) large than body size(0)");
  }

  // incorrect return type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x96',                   // incorrect return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    context->setBodySize(buffer.length());
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, context), EnvoyException,
                              "not supported return type 6");
  }

  // incorrect value size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x92',                   // without the value of the return type
        0x04, 't', 'e', 's', 't', // return body
    }));
    std::string exception_string =
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    buffer.length() - 1);
    context->setBodySize(buffer.length());
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

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  int type_value = *decoder.decode<int32_t>();
  EXPECT_EQ(static_cast<uint8_t>(mock_response_type), static_cast<uint8_t>(type_value));

  std::string content = *decoder.decode<std::string>();
  EXPECT_EQ(mock_response, content);

  EXPECT_EQ(buffer.length(), decoder.offset());

  size_t body_size = mock_response.size() + sizeof(mock_response_type);
  std::shared_ptr<ContextImpl> context = std::make_shared<ContextImpl>();
  context->setBodySize(body_size);
  auto result = serializer.deserializeRpcResult(buffer, context);
  EXPECT_TRUE(result.first->hasException());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
