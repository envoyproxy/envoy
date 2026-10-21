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
  MOCK_METHOD(SerializeType, type, (), (const, override));
  MOCK_METHOD(RpcRequestPtr, deserializeRpcRequest, (Buffer::Instance&, Context&), (override));
  MOCK_METHOD(RpcResponsePtr, deserializeRpcResponse, (Buffer::Instance&, Context&), (override));
  MOCK_METHOD(void, serializeRpcRequest, (Buffer::Instance&, MessageMetadata&), (override));
  MOCK_METHOD(void, serializeRpcResponse, (Buffer::Instance&, MessageMetadata&), (override));

  SerializeType type_{SerializeType::Hessian2};
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
