#pragma once

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "source/common/rds/common/route_config_provider_manager_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

using RouteConfigProviderManagerImpl = Rds::Common::RouteConfigProviderManagerImpl<
    envoy::extensions::filters::network::thrift_proxy::v3::Trds,
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, 1, ConfigImpl,
    NullConfigImpl>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
