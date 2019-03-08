#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

RpcInvocationImpl::~RpcInvocationImpl() {}
RpcInvocationImpl::RpcInvocationImpl(const std::string& method_name,
                                     const std::string& service_name,
                                     const std::string& service_version)
    : method_name_(method_name), service_name_(service_name), service_version_(service_version) {}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy