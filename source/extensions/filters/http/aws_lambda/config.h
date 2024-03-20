#pragma once

#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

class AwsLambdaFilterFactory
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::aws_lambda::v3::Config,
          envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig> {
public:
  AwsLambdaFilterFactory() : DualFactoryBase("envoy.filters.http.aws_lambda") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig& per_route_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor&) override;
};

using UpstreamAwsLambdaFilterFactory = AwsLambdaFilterFactory;

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
