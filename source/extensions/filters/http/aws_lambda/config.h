#pragma once

#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/credential_providers/config_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"
#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"
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
  Extensions::Common::Aws::CredentialsProviderChainSharedPtr getCredentialsProvider(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      Server::Configuration::ServerFactoryContext& server_context, const std::string& region) const;

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoHelper(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context,
      Stats::Scope& scope, bool is_upstream) const;

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig&,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

using UpstreamAwsLambdaFilterFactory = AwsLambdaFilterFactory;

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
