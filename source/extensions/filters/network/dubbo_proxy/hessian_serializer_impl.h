#pragma once

#include "extensions/filters/network/dubbo_proxy/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class HessianSerializerImpl : public Serialization {
public:
  HessianSerializerImpl(SerializationCallbacks& callbacks) : callbacks_(callbacks) {}
  ~HessianSerializerImpl() {}
  virtual const std::string& name() const override { return SerializerNames::get().HESSIAN; }
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size) override;
  virtual void deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) override;

private:
  SerializationCallbacks& callbacks_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy