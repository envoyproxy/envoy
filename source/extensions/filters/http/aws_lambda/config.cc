#include "source/extensions/filters/http/aws_lambda/config.h"

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/fmt.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "source/extensions/common/aws/utility.h"
#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {
constexpr auto service_name = "lambda";
namespace {

InvocationMode
getInvocationMode(const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config) {
  using namespace envoy::extensions::filters::http::aws_lambda::v3;
  switch (proto_config.invocation_mode()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case Config_InvocationMode_ASYNCHRONOUS:
    return InvocationMode::Asynchronous;
  case Config_InvocationMode_SYNCHRONOUS:
    return InvocationMode::Synchronous;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

Extensions::Common::Aws::CredentialsProviderSharedPtr
getCredentialsProvider(const std::string& profile,
                       Server::Configuration::ServerFactoryContext& server_context,
                       const std::string& region) {
  if (!profile.empty()) {
    return std::make_shared<Extensions::Common::Aws::CredentialsFileCredentialsProvider>(
        server_context.api(), profile);
  }
  return std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
      server_context.api(), makeOptRef(server_context), region,
      Extensions::Common::Aws::Utility::fetchMetadata);
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb> AwsLambdaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& server_context) {

  const auto arn = parseArn(proto_config.arn());
  if (!arn) {
    throw EnvoyException(fmt::format("aws_lambda_filter: Invalid ARN: {}", proto_config.arn()));
  }
  const std::string region = arn->region();

  auto credentials_provider =
      getCredentialsProvider(proto_config.credentials_profile(), server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider), server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{});

  FilterStats stats = generateStats(stats_prefix, dual_info.scope);
  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(proto_config), proto_config.payload_passthrough(),
      proto_config.host_rewrite(), stats, std::move(signer));

  return [filter_settings, dual_info](Http::FilterChainFactoryCallbacks& cb) -> void {
    auto filter = std::make_shared<Filter>(filter_settings, dual_info.is_upstream);
    cb.addStreamFilter(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
AwsLambdaFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig& per_route_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {

  const auto arn = parseArn(per_route_config.invoke_config().arn());
  if (!arn) {
    throw EnvoyException(
        fmt::format("aws_lambda_filter: Invalid ARN: {}", per_route_config.invoke_config().arn()));
  }

  const std::string region = arn->region();

  auto credentials_provider = getCredentialsProvider(
      per_route_config.invoke_config().credentials_profile(), server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider),
      server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{});

  FilterStats stats = generateStats(per_route_config.stat_prefix(), server_context.scope());
  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(per_route_config.invoke_config()),
      per_route_config.invoke_config().payload_passthrough(),
      per_route_config.invoke_config().host_rewrite(), stats, std::move(signer));

  return filter_settings;
}

/*
 * Static registration for the AWS Lambda filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsLambdaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAwsLambdaFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
