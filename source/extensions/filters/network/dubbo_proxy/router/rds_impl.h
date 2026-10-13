#pragma once

#include <memory>

#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.validate.h"

#include "source/common/rds/common/route_config_provider_manager_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/router/route_matcher.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

using RouteConfigProviderManagerImpl = Rds::Common::RouteConfigProviderManagerImpl<
    envoy::extensions::filters::network::dubbo_proxy::v3::Drds,
    envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration, 1,
    RouteConfigImpl, NullRouteConfigImpl>;

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
