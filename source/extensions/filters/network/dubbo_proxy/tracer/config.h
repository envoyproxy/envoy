#pragma once

#include <string>

#include "envoy/tracing/http_tracer_manager.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

class TracerFilterConfig : public DubboFilters::FactoryBase<
                               envoy::extensions::filters::network::http_connection_manager::v3::
                                   HttpConnectionManager::Tracing> {
public:
  TracerFilterConfig() : FactoryBase("envoy.filters.dubbo.tracer") {}

private:
  DubboFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::http_connection_manager::v3::
          HttpConnectionManager::Tracing& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
