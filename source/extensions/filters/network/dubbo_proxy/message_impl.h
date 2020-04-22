#pragma once

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ContextBase : public Context {
public:
  ContextBase() = default;
  ~ContextBase() override = default;

  // Override from Context
  size_t body_size() const override { return body_size_; }
  size_t header_size() const override { return header_size_; }

  void set_body_size(size_t size) { body_size_ = size; }
  void set_header_size(size_t size) { header_size_ = size; }

protected:
  size_t body_size_{0};
  size_t header_size_{0};
};

class ContextImpl : public ContextBase {
public:
  ContextImpl() = default;
  ~ContextImpl() override = default;

  bool is_heartbeat() const { return is_heartbeat_; }
  void set_heartbeat(bool is_heartbeat) { is_heartbeat_ = is_heartbeat; }

private:
  bool is_heartbeat_{false};
};

class RpcInvocationBase : public RpcInvocation {
public:
  ~RpcInvocationBase() override = default;

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& service_name() const override { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const std::string& method_name() const override { return method_name_; }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const absl::optional<std::string>& service_version() const override { return service_version_; }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const absl::optional<std::string>& service_group() const override { return group_; }

protected:
  std::string service_name_;
  std::string method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
