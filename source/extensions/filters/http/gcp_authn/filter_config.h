#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

class GcpAuthnFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig,
          envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterPerRouteConfig> {
public:
  GcpAuthnFilterFactory() : FactoryBase("envoy.filters.http.gcp_authn") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      const std::string&, FactoryContext& context) override;

  // TODO(tyxia) No need for perRouteConfig at this monment
  // Router::RouteSpecificFilterConfigConstSharedPtr
  // createRouteSpecificFilterConfigTyped(const GcpAuthnFilterPerRouteConfig& config,
  //                                      Server::Configuration::ServerFactoryContext&,
  //                                      ProtobufMessage::ValidationVisitor&) override;
};

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy