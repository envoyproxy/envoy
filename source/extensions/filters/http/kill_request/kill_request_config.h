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
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::kill_request::v3::KillRequest> {
public:
  KillRequestFilterFactory() : UnifiedFactoryBase("envoy.filters.http.kill_request") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
