#pragma once

#include "extensions/filters/network/dubbo_proxy/deserializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class HessianDeserializerImpl : public Deserializer {
public:
  HessianDeserializerImpl(DeserializationCallbacks& callbacks) : callbacks_(callbacks) {}
  ~HessianDeserializerImpl() {}
  virtual const std::string& name() const override { return DeserializerNames::get().Hessian; }
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size) override;
  virtual void deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) override;

private:
  DeserializationCallbacks& callbacks_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy