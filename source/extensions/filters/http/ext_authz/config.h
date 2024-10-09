#pragma once

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ext_authz::v3::ExtAuthz,
          envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute> {
public:
  ExtAuthzFilterConfig() : FactoryBase("envoy.filters.http.ext_authz") {}

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoWithServerContextTyped(proto_config, stats_prefix,
                                                              context.serverFactoryContext());
  }

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
