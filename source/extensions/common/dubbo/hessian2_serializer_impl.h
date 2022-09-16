#pragma once

#include "source/extensions/common/dubbo/message_impl.h"
#include "source/extensions/common/dubbo/serializer.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Hessian2SerializerImpl : public Serializer {
public:
  // Serializer
  SerializeType type() const override { return SerializeType::Hessian2; }
  RpcRequestSharedPtr deserializeRpcRequest(Buffer::Instance& buffer,
                                            MessageContext& context) override;
  RpcResponseSharedPtr deserializeRpcResponse(Buffer::Instance& buffer,
                                              MessageContext& context) override;
  void serializeRpcResponse(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  void serializeRpcRequest(Buffer::Instance& buffer, MessageMetadata& metadata) override;
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
