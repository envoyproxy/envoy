#pragma once

#include <memory>

#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"

#include "source/common/rds/common/route_config_provider_manager.h"

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
