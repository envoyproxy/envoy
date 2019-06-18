#pragma once

#include "extensions/filters/network/dubbo_proxy/deserializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class RpcResultImpl : public RpcResult {
public:
  RpcResultImpl() {}
  RpcResultImpl(bool has_exception) : has_exception_(has_exception) {}
  virtual bool hasException() const override { return has_exception_; }

private:
  bool has_exception_ = false;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy