#pragma once

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

struct ContextBase : public Context {
  ContextBase() = default;
  ~ContextBase() override {}

  // Override from Context
  size_t body_size() const override { return body_size_; }
  size_t header_size() const override { return header_size_; }

  size_t body_size_{0};
  size_t header_size_{0};
};

struct ContextImpl : public ContextBase {
  ContextImpl() = default;
  ~ContextImpl() override {}

  bool is_heartbeat_{false};
};

class RpcInvocationBase : public RpcInvocation {
public:
  ~RpcInvocationBase() override = default;

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& service_name() const override { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const absl::optional<std::string>& method_name() const override { return method_name_; }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const absl::optional<std::string>& service_version() const override { return service_version_; }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const absl::optional<std::string>& service_group() const override { return group_; }

protected:
  std::string service_name_;
  absl::optional<std::string> method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
