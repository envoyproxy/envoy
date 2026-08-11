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
class FaultFilterFactory : public Common::ExceptionFreeFactoryBase<
                               envoy::extensions::filters::http::fault::v3::HTTPFault> {
public:
  FaultFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.fault") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::fault::v3::HTTPFault& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
