#pragma once

#include "source/extensions/common/dubbo/serializer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class MockSerializer : public Serializer {
public:
  MockSerializer();
  ~MockSerializer() override;

  // DubboProxy::Serializer
  MOCK_METHOD(SerializeType, type, (), (const));
  MOCK_METHOD(RpcRequestPtr, deserializeRpcRequest, (Buffer::Instance&, Context&));
  MOCK_METHOD(RpcResponsePtr, deserializeRpcResponse, (Buffer::Instance&, Context&));
  MOCK_METHOD(void, serializeRpcRequest, (Buffer::Instance&, MessageMetadata&));
  MOCK_METHOD(void, serializeRpcResponse, (Buffer::Instance&, MessageMetadata&));

  SerializeType type_{SerializeType::Hessian2};
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
