#include "source/extensions/filters/http/aws_request_signing/config.h"

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/signer_impl.h"
#include "source/extensions/common/aws/utility.h"
#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

Http::FilterFactoryCb AwsRequestSigningFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          context.api(), Extensions::Common::Aws::Utility::fetchMetadata);
  const auto matcher_config = Extensions::Common::Aws::AwsSigV4HeaderExclusionVector(
      config.match_excluded_headers().begin(), config.match_excluded_headers().end());
  auto signer = std::make_unique<Extensions::Common::Aws::SignerImpl>(
      config.service_name(), config.region(), credentials_provider,
      context.mainThreadDispatcher().timeSource(), matcher_config);
  auto filter_config =
      std::make_shared<FilterConfigImpl>(std::move(signer), stats_prefix, context.scope(),
                                         config.host_rewrite(), config.use_unsigned_payload());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamDecoderFilter(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
AwsRequestSigningFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigningPerRoute& perRouteConfig,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          context.api(), Extensions::Common::Aws::Utility::fetchMetadata);
  const auto matcher_config = Extensions::Common::Aws::AwsSigV4HeaderExclusionVector(
      perRouteConfig.aws_request_signing().match_excluded_headers().begin(), perRouteConfig.aws_request_signing().match_excluded_headers().end());
  auto signer = std::make_unique<Extensions::Common::Aws::SignerImpl>(
      perRouteConfig.aws_request_signing().service_name(), perRouteConfig.aws_request_signing().region(), credentials_provider,
      context.mainThreadDispatcher().timeSource(), matcher_config);
  return std::make_shared<const FilterConfigImpl>(std::move(signer), perRouteConfig.stat_prefix(), context.scope(),
                                         perRouteConfig.aws_request_signing().host_rewrite(), perRouteConfig.aws_request_signing().use_unsigned_payload());
}

/**
 * Static registration for the AWS request signing filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsRequestSigningFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
