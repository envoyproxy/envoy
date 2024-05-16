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

} // namespace

// In case credentials from config or credentials_profile are set in the configuration, instead of
// using the default providers chain, it will use the credentials from config (if provided), then
// credentials file provider with the configured profile. All other providers will be ignored.
Extensions::Common::Aws::CredentialsProviderSharedPtr
AwsLambdaFilterFactory::getCredentialsProvider(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    Server::Configuration::ServerFactoryContext& server_context, const std::string& region) const {
  if (proto_config.has_credentials()) {
    ENVOY_LOG(debug,
              "credentials are set from filter configuration, default credentials providers chain "
              "will be ignored and only this credentials will be used");
    const auto& config_credentials = proto_config.credentials();
    return std::make_shared<Extensions::Common::Aws::ConfigCredentialsProvider>(
        config_credentials.access_key_id(), config_credentials.secret_access_key(),
        config_credentials.session_token());
  }
  if (!proto_config.credentials_profile().empty()) {
    ENVOY_LOG(debug,
              "credentials profile is set to \"{}\" in config, default credentials providers chain "
              "will be ignored and only credentials file provider will be used",
              proto_config.credentials_profile());
    return std::make_shared<Extensions::Common::Aws::CredentialsFileCredentialsProvider>(
        server_context.api(), proto_config.credentials_profile());
  }
  return std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
      server_context.api(), makeOptRef(server_context), region,
      Extensions::Common::Aws::Utility::fetchMetadata);
}

absl::StatusOr<Http::FilterFactoryCb> AwsLambdaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& server_context) {

  const auto arn = parseArn(proto_config.arn());
  if (!arn) {
    throw EnvoyException(fmt::format("aws_lambda_filter: Invalid ARN: {}", proto_config.arn()));
  }
  const std::string region = arn->region();

  auto credentials_provider = getCredentialsProvider(proto_config, server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider), server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{});

  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(proto_config), proto_config.payload_passthrough(),
      proto_config.host_rewrite(), std::move(signer));

  FilterStats stats = generateStats(stats_prefix, dual_info.scope);
  return [stats, filter_settings, dual_info](Http::FilterChainFactoryCallbacks& cb) -> void {
    auto filter = std::make_shared<Filter>(filter_settings, stats, dual_info.is_upstream);
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
  auto credentials_provider =
      getCredentialsProvider(per_route_config.invoke_config(), server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider), server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{});

  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(per_route_config.invoke_config()),
      per_route_config.invoke_config().payload_passthrough(),
      per_route_config.invoke_config().host_rewrite(), std::move(signer));

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
