#pragma once

#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

class AwsLambdaFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::aws_lambda::v3::Config,
                                 envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig> {
public:
  AwsLambdaFilterFactory() : FactoryBase(HttpFilterNames::get().AwsLambda) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig&,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
