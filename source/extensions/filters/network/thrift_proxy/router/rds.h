#pragma once

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "source/common/rds/common/route_config_provider_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

using RouteConfigProviderManager = Rds::Common::RouteConfigProviderManager<
    envoy::extensions::filters::network::thrift_proxy::v3::Trds,
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
