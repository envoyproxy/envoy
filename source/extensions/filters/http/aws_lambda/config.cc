#include "source/extensions/filters/http/aws_lambda/config.h"

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/fmt.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/signer_impl.h"
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

Http::FilterFactoryCb AwsLambdaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();

  const auto arn = parseArn(proto_config.arn());
  if (!arn) {
    throw EnvoyException(fmt::format("aws_lambda_filter: Invalid ARN: {}", proto_config.arn()));
  }
  const std::string region = arn->region();

  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          server_context.api(), makeOptRef(server_context), region,
          Extensions::Common::Aws::Utility::fetchMetadata);

  auto signer = std::make_shared<Extensions::Common::Aws::SignerImpl>(
      service_name, region, std::move(credentials_provider),
      server_context.mainThreadDispatcher().timeSource(),
      // TODO: extend API to allow specifying header exclusion. ref:
      // https://github.com/envoyproxy/envoy/pull/18998
      Extensions::Common::Aws::AwsSigV4HeaderExclusionVector{});

  FilterSettings filter_settings{*arn, getInvocationMode(proto_config),
                                 proto_config.payload_passthrough()};

  FilterStats stats = generateStats(stat_prefix, context.scope());
  return [stats, signer, filter_settings](Http::FilterChainFactoryCallbacks& cb) {
    auto filter = std::make_shared<Filter>(filter_settings, stats, signer);
    cb.addStreamFilter(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
AwsLambdaFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {

  const auto arn = parseArn(proto_config.invoke_config().arn());
  if (!arn) {
    throw EnvoyException(
        fmt::format("aws_lambda_filter: Invalid ARN: {}", proto_config.invoke_config().arn()));
  }
  return std::make_shared<const FilterSettings>(
      FilterSettings{*arn, getInvocationMode(proto_config.invoke_config()),
                     proto_config.invoke_config().payload_passthrough()});
}

/*
 * Static registration for the AWS Lambda filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsLambdaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
