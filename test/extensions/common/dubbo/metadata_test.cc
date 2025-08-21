#include <memory>

#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(ContextTest, ContextTest) {
  Context context;

  // Simple set and get of message type.
  context.setMessageType(MessageType::HeartbeatResponse);
  EXPECT_EQ(MessageType::HeartbeatResponse, context.messageType());

  // Simple set and get of response status
  EXPECT_EQ(false, context.hasResponseStatus());
  context.setResponseStatus(ResponseStatus::BadRequest);
  EXPECT_EQ(true, context.hasResponseStatus());
  EXPECT_EQ(ResponseStatus::BadRequest, context.responseStatus());

  // Simple set and get of body size.
  context.setBodySize(12345);
  EXPECT_EQ(12345, context.bodySize());

  // Simple set and get of request id.
  context.setRequestId(12345);
  EXPECT_EQ(12345, context.requestId());

  // Two way request check.
  context.setMessageType(MessageType::Oneway);
  EXPECT_EQ(false, context.isTwoWay());
  context.setMessageType(MessageType::Response);
  EXPECT_EQ(false, context.isTwoWay());
  context.setMessageType(MessageType::HeartbeatRequest);
  EXPECT_EQ(false, context.isTwoWay());
  context.setMessageType(MessageType::Request);
  EXPECT_EQ(true, context.isTwoWay());

  // Heartbeat check.
  context.setMessageType(MessageType::Request);
  EXPECT_EQ(false, context.heartbeat());
  context.setMessageType(MessageType::Response);
  EXPECT_EQ(false, context.heartbeat());
  context.setMessageType(MessageType::HeartbeatResponse);
  EXPECT_EQ(true, context.heartbeat());
  context.setMessageType(MessageType::HeartbeatRequest);
  EXPECT_EQ(true, context.heartbeat());
}

TEST(MessageMetadataTest, MessageMetadataTest) {
  MessageMetadata metadata;

  auto context = std::make_unique<Context>();
  auto raw_context = context.get();

  auto request = std::make_unique<RpcRequest>("a", "b", "c", "d");
  auto raw_request = request.get();

  auto response = std::make_unique<RpcResponse>();
  auto raw_response = response.get();

  // Simple set and get of context.
  EXPECT_EQ(false, metadata.hasContext());
  metadata.setContext(std::move(context));
  EXPECT_EQ(true, metadata.hasContext());
  EXPECT_EQ(raw_context, &metadata.context());
  EXPECT_EQ(raw_context, &metadata.mutableContext());

  // Simple set and get of request.
  EXPECT_EQ(false, metadata.hasRequest());
  metadata.setRequest(std::move(request));
  EXPECT_EQ(true, metadata.hasRequest());
  EXPECT_EQ(raw_request, &metadata.request());
  EXPECT_EQ(raw_request, &metadata.mutableRequest());

  // Simple set and get of response.
  EXPECT_EQ(false, metadata.hasResponse());
  metadata.setResponse(std::move(response));
  EXPECT_EQ(true, metadata.hasResponse());
  EXPECT_EQ(raw_response, &metadata.response());
  EXPECT_EQ(raw_response, &metadata.mutableResponse());

  raw_context->setRequestId(12345);
  raw_context->setMessageType(MessageType::Exception);
  raw_context->setResponseStatus(ResponseStatus::ServerError);

  // Simple test of help methods.
  EXPECT_EQ(raw_context->messageType(), metadata.messageType());
  EXPECT_EQ(raw_context->hasResponseStatus(), metadata.hasResponseStatus());
  EXPECT_EQ(raw_context->responseStatus(), metadata.responseStatus());
  EXPECT_EQ(raw_context->heartbeat(), metadata.heartbeat());
  EXPECT_EQ(raw_context->requestId(), metadata.requestId());
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
