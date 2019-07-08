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
  virtual const std::string& name() const override {
    return DeserializerNames::get().fromType(type());
  }
  virtual SerializationType type() const override { return SerializationType::Hessian; }
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size,
                                        MessageMetadataSharedPtr metadata) override;
  virtual RpcResultPtr deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) override;
  virtual size_t serializeRpcResult(Buffer::Instance& output_buffer, const std::string& content,
                                    RpcResponseType type) override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy