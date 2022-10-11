#include <memory>

#include "source/extensions/common/dubbo/hessian2_serializer_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(Hessian2ProtocolTest, Type) {
  Hessian2SerializerImpl serializer;
  EXPECT_EQ(SerializeType::Hessian2, serializer.type());
}

TEST(Hessian2ProtocolTest, deserializeRpcRequest) {
  Hessian2SerializerImpl serializer;

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x05, '2', '.', '0', '.', '2', // Dubbo version
        0x04, 't', 'e', 's', 't',      // Service name
        0x05, '0', '.', '0', '.', '0', // Service version
        0x04, 't', 'e', 's', 't',      // method name
    }));
    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());
    auto result = serializer.deserializeRpcRequest(buffer, *context);
    ASSERT(result != nullptr);

    EXPECT_EQ("test", result->methodName());
    EXPECT_EQ("test", result->serviceName());
    EXPECT_EQ("0.0.0", result->serviceVersion());
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
    std::string exception_string = fmt::format("RpcRequest size({}) larger than body size({})",
                                               buffer.length(), buffer.length() - 1);
    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length() - 1);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcRequest(buffer, *context), EnvoyException,
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
    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcRequest(buffer, *context), EnvoyException,
                              "RpcRequest has no request metadata");
  }
}

TEST(Hessian2ProtocolTest, deserializeRpcRequestWithParametersOrAttachment) {
  Hessian2SerializerImpl serializer;

  RpcRequestImpl::Attachment attach(std::make_unique<RpcRequestImpl::Attachment::Map>(), 0);
  attach.insert("test1", "test_value1");
  attach.insert("test2", "test_value2");
  attach.insert("test3", "test_value3");

  RpcRequestImpl::Parameters params;

  params.push_back(std::make_unique<Hessian2::StringObject>("test_string"));

  std::vector<uint8_t> test_binary{0, 1, 2, 3, 4};
  params.push_back(std::make_unique<Hessian2::BinaryObject>(test_binary));

  params.push_back(std::make_unique<Hessian2::LongObject>(233333));

  // 4 parameters. Some times we will encode attachment as a map type parameter for test.
  std::string parameters_type = "Ljava.lang.String;[BJLjava.util.Map;";

  // Test for heartbeat request.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything_here_for_heartbeat");

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());
    context->setMessageType(MessageType::HeartbeatRequest);

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_EQ(nullptr, result);

    EXPECT_EQ(0, buffer.length());
  }

  {
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

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_NE(nullptr, result);

    auto invo = dynamic_cast<RpcRequestImpl*>(result.get());

    // All data be moved to buffer in the request.
    EXPECT_EQ(0, buffer.length());
    EXPECT_EQ(context->bodySize(), invo->messageBuffer().length());

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

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_NE(nullptr, result);

    auto invo = dynamic_cast<RpcRequestImpl*>(result.get());

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

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_NE(nullptr, result);

    auto invo = dynamic_cast<RpcRequestImpl*>(result.get());

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

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_NE(nullptr, result);

    auto invo = dynamic_cast<RpcRequestImpl*>(result.get());

    // There are not enough parameters and throws an exception.
    EXPECT_THROW_WITH_MESSAGE(invo->mutableParameters(), EnvoyException,
                              "Cannot parse RpcRequest parameter from buffer");
  }
  // Test for incorrect attachment types.
  {
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

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcRequest(buffer, *context);
    EXPECT_NE(nullptr, result);

    auto invo = dynamic_cast<RpcRequestImpl*>(result.get());

    auto& result_attach = invo->mutableAttachment();
    EXPECT_EQ(true, result_attach->attachment().toUntypedMap().value().get().empty());
  }
}

TEST(Hessian2ProtocolTest, deserializeRpcResponse) {
  Hessian2SerializerImpl serializer;

  // Test for heartbeat response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add("anything_here_for_heartbeat");

    auto context = std::make_unique<Context>();
    context->setBodySize(buffer.length());
    context->setMessageType(MessageType::HeartbeatResponse);
    context->setResponseStatus(ResponseStatus::Ok);

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_EQ(nullptr, result);

    EXPECT_EQ(0, buffer.length());
  }

  // The first element by of normal response should response type.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x04,
        't',
        'e',
        's',
        't',
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResponse(buffer, *context), EnvoyException,
                              "Cannot parse RpcResponse type from buffer");
  }

  // If a response is set to type `Exception` before calling `deserializeRpcRequest`, then
  // it must be a non-Ok request and the response type would absent.
  {

    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        0x04,
        't',
        'e',
        's',
        't',
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Exception);
    context->setResponseStatus(ResponseStatus::BadResponse);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);
    EXPECT_EQ(0, buffer.length());
  }

  // Normal response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseValueWithAttachments, result->responseType().value());
    EXPECT_EQ(MessageType::Response, context->messageType());
  }

  // Exception response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x93',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithExceptionWithAttachments,
              result->responseType().value());
    // The message type will be set to exception if there is response with exception.
    EXPECT_EQ(MessageType::Exception, context->messageType());
  }

  // Exception response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x90',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithException, result->responseType().value());
    EXPECT_EQ(MessageType::Exception, context->messageType());
  }

  // Normal response.
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x91',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseWithValue, result->responseType().value());
    EXPECT_EQ(MessageType::Response, context->messageType());
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

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    auto result = serializer.deserializeRpcResponse(buffer, *context);
    EXPECT_NE(nullptr, result);

    EXPECT_EQ(RpcResponseType::ResponseNullValueWithAttachments, result->responseType().value());
    EXPECT_EQ(MessageType::Response, context->messageType());
  }

  // Incorrect body size
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x94',                   // return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(0);

    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResponse(buffer, *context), EnvoyException,
                              "RpcResponse size(1) large than body size(0)");
  }

  // Incorrect return type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({
        '\x96',                   // incorrect return type
        0x04, 't', 'e', 's', 't', // return body
    }));

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResponse(buffer, *context), EnvoyException,
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
        fmt::format("RpcResponse is no value, but the rest of the body size({}) not equal 0",
                    buffer.length() - 1);

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);
    context->setBodySize(buffer.length());

    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResponse(buffer, *context), EnvoyException,
                              exception_string);
  }
}

TEST(Hessian2ProtocolTest, serializeRpcRequest) {
  Hessian2SerializerImpl serializer;

  // Heartbeat request.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::HeartbeatRequest);
    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, *metadata);

    EXPECT_EQ(1, buffer.length());
    EXPECT_EQ("N", buffer.toString());
  }

  // Normal request.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);

    auto request = std::make_unique<RpcRequestImpl>();
    request->setServiceName("test.service");
    request->setMethodName("test.method");
    request->setServiceVersion("test.version");
    request->messageBuffer().add("anything_for_no_attachment_update");

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));
    metadata->setRequest(std::move(request));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, *metadata);

    // No attachment update and the message buffer will be used directly to
    // accelerate the encoding.
    EXPECT_EQ("anything_for_no_attachment_update", buffer.toString());
  }

  // Normal request with attachment update.
  {

    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Request);

    auto request = std::make_unique<RpcRequestImpl>();
    request->setServiceName("test.service");
    request->setMethodName("test.method");
    request->setServiceVersion("test.version");
    request->messageBuffer().add("anything_for_attachment_update");

    size_t fake_attachment_offset = 8;

    request->setParametersLazyCallback([]() -> RpcRequestImpl::ParametersPtr {
      return std::make_unique<RpcRequestImpl::Parameters>();
    });

    request->setAttachmentLazyCallback([fake_attachment_offset]() -> RpcRequestImpl::AttachmentPtr {
      return std::make_unique<RpcRequestImpl::Attachment>(
          std::make_unique<RpcRequestImpl::Attachment::Map>(), fake_attachment_offset);
    });
    request->mutableAttachment()->insert("key", "value");

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));
    metadata->setRequest(std::move(request));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcRequest(buffer, *metadata);

    // 8 (fake_attachment_offset) bytes for original parameters and 12 bytes for updated attachment.
    EXPECT_EQ(20, buffer.length());

    EXPECT_EQ(true, absl::StrContains(buffer.toString(), "value"));
  }
}

TEST(Hessian2ProtocolTest, serializeRpcResponse) {
  Hessian2SerializerImpl serializer;

  // Heartbeat response.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::HeartbeatResponse);
    context->setResponseStatus(ResponseStatus::Ok);
    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, *metadata);

    EXPECT_EQ(1, buffer.length());
    EXPECT_EQ("N", buffer.toString());
  }

  // Normal response.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);

    auto response = std::make_unique<RpcResponseImpl>();
    response->messageBuffer().add("anything");

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));
    metadata->setResponse(std::move(response));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, *metadata);

    // The data in message buffer will be used directly for normal response.
    EXPECT_EQ("anything", buffer.toString());
  }

  // Local response without response type.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);

    auto response = std::make_unique<RpcResponseImpl>();
    response->setLocalRawMessage("anything");

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));
    metadata->setResponse(std::move(response));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, *metadata);

    auto buffer_string = buffer.toString();

    EXPECT_EQ(0x08, static_cast<uint8_t>(buffer_string[0])); // Check length.
    EXPECT_EQ("anything", buffer_string.substr(1));
  }

  // Local response with response type.
  {
    auto context = std::make_unique<Context>();
    context->setMessageType(MessageType::Response);
    context->setResponseStatus(ResponseStatus::Ok);

    auto response = std::make_unique<RpcResponseImpl>();
    response->setResponseType(RpcResponseType::ResponseWithException);
    response->setLocalRawMessage("anything");

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setContext(std::move(context));
    metadata->setResponse(std::move(response));

    Buffer::OwnedImpl buffer;
    serializer.serializeRpcResponse(buffer, *metadata);

    auto buffer_string = buffer.toString();

    EXPECT_EQ(0x90, static_cast<uint8_t>(buffer_string[0])); // Check response type.
    EXPECT_EQ(0x08, static_cast<uint8_t>(buffer_string[1])); // Check length.
    EXPECT_EQ("anything", buffer_string.substr(2));
  }
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
