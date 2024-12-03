#pragma once

#include <memory>

#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"

#include "source/common/rds/common/route_config_provider_manager_impl.h"
#include "source/extensions/filters/network/generic_proxy/route_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using RouteConfigProviderManagerImpl = Rds::Common::RouteConfigProviderManagerImpl<
    envoy::extensions::filters::network::generic_proxy::v3::GenericRds,
    envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration, 1, RouteMatcherImpl,
    NullRouteMatcherImpl>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
