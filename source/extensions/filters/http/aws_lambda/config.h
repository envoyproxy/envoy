#pragma once

#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

class AwsLambdaFilterFactory
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::aws_lambda::v3::Config,
          envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig>,
      Logger::Loggable<Logger::Id::filter> {
public:
  AwsLambdaFilterFactory() : DualFactoryBase("envoy.filters.http.aws_lambda") {}

protected:
  Extensions::Common::Aws::CredentialsProviderSharedPtr getCredentialsProvider(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      Server::Configuration::ServerFactoryContext& server_context, const std::string& region) const;

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig&,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

using UpstreamAwsLambdaFilterFactory = AwsLambdaFilterFactory;

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
