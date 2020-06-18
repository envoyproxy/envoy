#pragma once

#include "extensions/filters/network/dubbo_proxy/message_impl.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class RpcInvocationImpl : public RpcInvocationBase {
public:
  // TODO(gengleilei) Add parameter data types and implement Dubbo data type mapping.
  using ParameterValueMap = std::unordered_map<uint32_t, std::string>;
  using ParameterValueMapPtr = std::unique_ptr<ParameterValueMap>;

  RpcInvocationImpl() = default;
  ~RpcInvocationImpl() override = default;

  void addParameterValue(uint32_t index, const std::string& value);
  const ParameterValueMap& parameters();
  const std::string& getParameterValue(uint32_t index) const;
  bool hasParameters() const { return parameter_map_ != nullptr; }

  void addHeader(const std::string& key, const std::string& value);
  void addHeaderReference(const Http::LowerCaseString& key, const std::string& value);
  const Http::HeaderMap& headers() const;
  bool hasHeaders() const { return headers_ != nullptr; }

private:
  inline void assignHeaderIfNeed() {
    if (!headers_) {
      headers_ = Http::RequestHeaderMapImpl::create();
    }
  }

  inline void assignParameterIfNeed() {
    if (!parameter_map_) {
      parameter_map_ = std::make_unique<ParameterValueMap>();
    }
  }

  ParameterValueMapPtr parameter_map_;
  Http::HeaderMapPtr headers_; // attachment
};

class RpcResultImpl : public RpcResult {
public:
  RpcResultImpl() = default;
  ~RpcResultImpl() override = default;

  bool hasException() const override { return has_exception_; }
  void setException(bool has_exception) { has_exception_ = has_exception; }

private:
  bool has_exception_ = false;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
