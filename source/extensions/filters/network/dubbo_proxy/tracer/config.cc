#include "source/extensions/filters/network/dubbo_proxy/tracer/config.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/registry/registry.h"
#include "source/extensions/filters/network/dubbo_proxy/tracer/tracer_impl.h"
#include "source/common/common/logger.h"
#include "envoy/singleton/manager.h"
#include "source/common/tracing/http_tracer_manager_impl.h"
#include "source/common/tracing/tracer_config_impl.h"
#include "source/common/tracing/custom_tag_impl.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

DubboFilters::FilterFactoryCb TracerFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        Tracing&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](DubboFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addFilter(std::make_shared<Tracer>());
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(TracerFilterConfig, DubboFilters::NamedDubboFilterConfigFactory);

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
