#pragma once

#include "extensions/filters/network/dubbo_proxy/deserializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class RpcInvocationImpl : public RpcInvocation {
public:
  ~RpcInvocationImpl();
  RpcInvocationImpl(const std::string& method_name, const std::string& service_name,
                    const std::string& service_version);
  virtual const std::string& getMethodName() const override { return method_name_; }
  virtual const std::string& getServiceName() const override { return service_name_; }
  virtual const std::string& getServiceVersion() const override { return service_version_; }

private:
  std::string method_name_;
  std::string service_name_;
  std::string service_version_;
};

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