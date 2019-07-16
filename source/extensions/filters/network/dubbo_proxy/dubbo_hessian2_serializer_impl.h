#pragma once

#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
class DubboHessian2SerializerImpl : public Serializer {
public:
  ~DubboHessian2SerializerImpl() = default;
  virtual const std::string& name() const override {
    return ProtocolSerializerNames::get().fromType(ProtocolType::Dubbo, type());
  }
  virtual SerializationType type() const override { return SerializationType::Hessian2; }

  virtual std::pair<RpcInvocationSharedPtr, bool>
  deserializeRpcInvocation(Buffer::Instance& buffer, ContextSharedPtr context) override;

  virtual std::pair<RpcResultSharedPtr, bool>
  deserializeRpcResult(Buffer::Instance& buffer, ContextSharedPtr context) override;

  virtual size_t serializeRpcResult(Buffer::Instance& output_buffer, const std::string& content,
                                    RpcResponseType type) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy