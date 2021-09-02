#pragma once

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

/**
 * Config registration for KillRequestFilter. @see NamedHttpFilterConfigFactory.
 */
class KillRequestFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::kill_request::v3::KillRequest> {
public:
  KillRequestFilterFactory() : FactoryBase("envoy.filters.http.kill_request") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
