#include "extensions/filters/network/dubbo_proxy/serializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

void RpcInvocationImpl::addParameterValue(uint32_t index, const std::string& value) {
  assignParameterIfNeed();
  parameter_map_->emplace(index, value);
}

const std::string& RpcInvocationImpl::getParameterValue(uint32_t index) const {
  if (parameter_map_) {
    auto itor = parameter_map_->find(index);
    if (itor != parameter_map_->end()) {
      return itor->second;
    }
  }

  return EMPTY_STRING;
}

const RpcInvocationImpl::ParameterValueMap& RpcInvocationImpl::parameters() {
  ASSERT(hasParameters());
  return *parameter_map_;
}

const Http::HeaderMap& RpcInvocationImpl::headers() const {
  ASSERT(hasHeaders());
  return *headers_;
}

void RpcInvocationImpl::addHeader(const std::string& key, const std::string& value) {
  assignHeaderIfNeed();
  headers_->addCopy(Http::LowerCaseString(key), value);
}

void RpcInvocationImpl::addHeaderReference(const Http::LowerCaseString& key,
                                           const std::string& value) {
  assignHeaderIfNeed();
  headers_->addReference(key, value);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
