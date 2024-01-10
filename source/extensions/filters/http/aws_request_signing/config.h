#pragma once

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

using AwsRequestSigningProtoConfig =
    envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning;

using AwsRequestSigningProtoPerRouteConfig =
    envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigningPerRoute;

/**
 * Config registration for the AWS request signing filter.
 */
class AwsRequestSigningFilterFactory
    : public Common::DualFactoryBase<AwsRequestSigningProtoConfig,
                                     AwsRequestSigningProtoPerRouteConfig> {
public:
  AwsRequestSigningFilterFactory() : DualFactoryBase("envoy.filters.http.aws_request_signing") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const AwsRequestSigningProtoConfig& proto_config,
                                    const std::string& stats_prefix, DualInfo dual_info,
                                    Server::Configuration::ServerFactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const AwsRequestSigningProtoPerRouteConfig& per_route_config,
                                       Server::Configuration::ServerFactoryContext& context,
                                       ProtobufMessage::ValidationVisitor&) override;
};

using UpstreamAwsRequestSigningFilterFactory = AwsRequestSigningFilterFactory;

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
