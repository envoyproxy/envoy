#pragma once

#include <memory>

#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"

#include "source/common/rds/common/route_config_provider_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

using RouteConfigProviderManager = Rds::Common::RouteConfigProviderManager<
    envoy::extensions::filters::network::dubbo_proxy::v3::Drds,
    envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration>;

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
