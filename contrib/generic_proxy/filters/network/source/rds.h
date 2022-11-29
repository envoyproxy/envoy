#pragma once

#include <memory>

#include "source/common/rds/common/route_config_provider_manager.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using RouteConfigProviderManager = Rds::Common::RouteConfigProviderManager<
    envoy::extensions::filters::network::generic_proxy::v3::GenericRds,
    envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
