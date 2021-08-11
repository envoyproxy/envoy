#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DownstreamConnectionCloseException::DownstreamConnectionCloseException(const std::string& what)
    : EnvoyException(what) {}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
