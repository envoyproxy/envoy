#pragma once

#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * Config registration for the fault injection filter. @see NamedHttpFilterConfigFactory.
 */
class FaultFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::fault::v3::HTTPFault> {
public:
  FaultFilterFactory() : FactoryBase("envoy.filters.http.fault") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
