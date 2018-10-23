#pragma once

#include "extensions/filters/network/dubbo_proxy/deserializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
class HessianDeserializerImpl : public Deserializer {
public:
  HessianDeserializerImpl() {}
  ~HessianDeserializerImpl() {}
  virtual const std::string& name() const override { return DeserializerNames::get().Hessian; }
  virtual RpcInvocationPtr deserializeRpcInvocation(Buffer::Instance& buffer,
                                                    size_t body_size) override;
  virtual RpcResultPtr deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy