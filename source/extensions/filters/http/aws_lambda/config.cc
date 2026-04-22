#include "source/extensions/filters/http/aws_lambda/config.h"

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
Extensions::Common::Aws::CredentialsProviderChainSharedPtr
AwsLambdaFilterFactory::getCredentialsProvider(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    Server::Configuration::ServerFactoryContext& server_context, const std::string& region) const {
  if (proto_config.has_credentials()) {
    ENVOY_LOG(debug,
              "credentials are set from filter configuration, default credentials providers chain "
              "will be ignored and only this credentials will be used");
    const auto& config_credentials = proto_config.credentials();
    auto chain = std::make_shared<Extensions::Common::Aws::CredentialsProviderChain>();
    chain->add(std::make_shared<Extensions::Common::Aws::ConfigCredentialsProvider>(
        config_credentials.access_key_id(), config_credentials.secret_access_key(),
        config_credentials.session_token()));
    return chain;
  }
  if (!proto_config.credentials_profile().empty()) {
    ENVOY_LOG(debug,
              "credentials profile is set to \"{}\" in config, default credentials providers chain "
              "will be ignored and only credentials file provider will be used",
              proto_config.credentials_profile());
    envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider credential_file_config;
    credential_file_config.set_profile(proto_config.credentials_profile());
    auto chain = std::make_shared<Extensions::Common::Aws::CredentialsProviderChain>();
    chain->add(std::make_shared<Extensions::Common::Aws::CredentialsFileCredentialsProvider>(
        server_context, credential_file_config));
    return chain;
  }
  return Extensions::Common::Aws::CommonCredentialsProviderChain::defaultCredentialsProviderChain(
      server_context, region);
}

absl::StatusOr<Http::FilterFactoryCb> AwsLambdaFilterFactory::createFilterFactoryFromProtoHelper(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context,
    Stats::Scope& scope, bool is_upstream) const {

  const auto arn = parseArn(proto_config.arn());
  if (!arn) {
    return absl::InvalidArgumentError(
        fmt::format("aws_lambda_filter: Invalid ARN: {}", proto_config.arn()));
  }
  const std::string region = arn->region();

  auto credentials_provider = getCredentialsProvider(proto_config, server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider), server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(proto_config), proto_config.payload_passthrough(),
      proto_config.host_rewrite(), std::move(signer));

  FilterStats stats = generateStats(stats_prefix, scope);
  return [stats, filter_settings, is_upstream](Http::FilterChainFactoryCallbacks& cb) -> void {
    auto filter = std::make_shared<Filter>(filter_settings, stats, is_upstream);
    cb.addStreamFilter(filter);
  };
}

absl::StatusOr<Http::FilterFactoryCb> AwsLambdaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& server_context) {
  return createFilterFactoryFromProtoHelper(proto_config, stats_prefix, server_context,
                                            dual_info.scope, dual_info.is_upstream);
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
AwsLambdaFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig& per_route_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {

  const auto arn = parseArn(per_route_config.invoke_config().arn());
  if (!arn) {
    return absl::InvalidArgumentError(
        fmt::format("aws_lambda_filter: Invalid ARN: {}", per_route_config.invoke_config().arn()));
  }
  const std::string region = arn->region();
  auto credentials_provider =
      getCredentialsProvider(per_route_config.invoke_config(), server_context, region);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name, region, std::move(credentials_provider), server_context,
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

  auto filter_settings = std::make_shared<FilterSettingsImpl>(
      *arn, getInvocationMode(per_route_config.invoke_config()),
      per_route_config.invoke_config().payload_passthrough(),
      per_route_config.invoke_config().host_rewrite(), std::move(signer));

  return filter_settings;
}

Http::FilterFactoryCb AwsLambdaFilterFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context) {
  auto result = createFilterFactoryFromProtoHelper(proto_config, stats_prefix, server_context,
                                                   server_context.scope(), false);
  if (!result.ok()) {
    ExceptionUtil::throwEnvoyException(std::string(result.status().message()));
  }
  return std::move(result.value());
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
