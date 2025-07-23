#include "source/extensions/filters/http/aws_request_signing/config.h"

#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

using namespace envoy::extensions::filters::http::aws_request_signing::v3;

bool isARegionSet(std::string region) {
  for (const char& c : "*,") {
    if (region.find(c) != std::string::npos) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<Http::FilterFactoryCb>
AwsRequestSigningFilterFactory::createFilterFactoryFromProtoTyped(
    const AwsRequestSigningProtoConfig& config, const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& server_context) {

  auto signer = createSigner(config, server_context);
  if (!signer.ok()) {
    return absl::InvalidArgumentError(std::string(signer.status().message()));
  }
  auto filter_config =
      std::make_shared<FilterConfigImpl>(std::move(signer.value()), stats_prefix, dual_info.scope,
                                         config.host_rewrite(), config.use_unsigned_payload());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamDecoderFilter(filter);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
AwsRequestSigningFilterFactory::createRouteSpecificFilterConfigTyped(
    const AwsRequestSigningProtoPerRouteConfig& per_route_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {

  auto signer = createSigner(per_route_config.aws_request_signing(), server_context);
  if (!signer.ok()) {
    return absl::InvalidArgumentError(std::string(signer.status().message()));
  }

  return std::make_shared<const FilterConfigImpl>(
      std::move(signer.value()), per_route_config.stat_prefix(), server_context.scope(),
      per_route_config.aws_request_signing().host_rewrite(),
      per_route_config.aws_request_signing().use_unsigned_payload());
}

absl::StatusOr<Envoy::Extensions::Common::Aws::SignerPtr>
AwsRequestSigningFilterFactory::createSigner(
    const AwsRequestSigningProtoConfig& config,
    Server::Configuration::ServerFactoryContext& server_context) {

  std::string region = config.region();

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  // If we have an overriding credential provider configuration, read it here as it may contain
  // references to the region
  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider credential_file_config = {};
  if (config.has_credential_provider()) {
    if (config.credential_provider().has_credentials_file_provider()) {
      credential_file_config = config.credential_provider().credentials_file_provider();
    }
  }

  if (region.empty()) {
    auto region_provider =
        std::make_shared<Extensions::Common::Aws::RegionProviderChain>(credential_file_config);
    absl::optional<std::string> regionOpt;
    if (config.signing_algorithm() == AwsRequestSigning_SigningAlgorithm_AWS_SIGV4A) {
      regionOpt = region_provider->getRegionSet();
    } else {
      regionOpt = region_provider->getRegion();
    }
    if (!regionOpt.has_value()) {
      return absl::InvalidArgumentError(
          "AWS region is not set in xDS configuration and failed to retrieve from "
          "environment variable or AWS profile/config files.");
    }
    region = regionOpt.value();
  }

  absl::StatusOr<Envoy::Extensions::Common::Aws::CredentialsProviderChainSharedPtr>
      credentials_provider =
          absl::InvalidArgumentError("No credentials provider settings configured.");

  const bool has_credential_provider_settings =
      config.has_credential_provider() &&
      (config.credential_provider().has_assume_role_with_web_identity_provider() ||
       config.credential_provider().has_credentials_file_provider());

  if (config.has_credential_provider()) {
    if (config.credential_provider().has_inline_credential()) {
      // If inline credential provider is set, use it instead of the default or custom credentials
      // chain
      const auto& inline_credential = config.credential_provider().inline_credential();
      credentials_provider = std::make_shared<Extensions::Common::Aws::CredentialsProviderChain>();
      auto inline_provider = std::make_shared<Extensions::Common::Aws::InlineCredentialProvider>(
          inline_credential.access_key_id(), inline_credential.secret_access_key(),
          inline_credential.session_token());
      credentials_provider.value()->add(inline_provider);

    } else if (config.credential_provider().custom_credential_provider_chain()) {
      // Custom credential provider chain
      if (has_credential_provider_settings) {
        credentials_provider =
            std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
                server_context, region, config.credential_provider());
      }
    } else {
      // Override default credential provider chain settings with any provided settings
      if (has_credential_provider_settings) {
        credential_provider_config = config.credential_provider();
      }
      credentials_provider =
          std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
              server_context.api(), server_context, region, credential_provider_config);
    }
  } else {
    // No credential provider settings provided, so make the default credentials provider chain
    credentials_provider =
        std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
            server_context.api(), server_context, region, credential_provider_config);
  }

  if (!credentials_provider.ok()) {
    return absl::InvalidArgumentError(std::string(credentials_provider.status().message()));
  }

  const auto matcher_config = Extensions::Common::Aws::AwsSigningHeaderExclusionVector(
      config.match_excluded_headers().begin(), config.match_excluded_headers().end());

  const bool query_string = config.has_query_string();

  const uint16_t expiration_time = PROTOBUF_GET_SECONDS_OR_DEFAULT(
      config.query_string(), expiration_time,
      Extensions::Common::Aws::SignatureQueryParameterValues::DefaultExpiration);

  std::unique_ptr<Extensions::Common::Aws::Signer> signer;

  if (config.signing_algorithm() == AwsRequestSigning_SigningAlgorithm_AWS_SIGV4A) {
    return std::make_unique<Extensions::Common::Aws::SigV4ASignerImpl>(
        config.service_name(), region, credentials_provider.value(), server_context, matcher_config,
        query_string, expiration_time);
  } else {
    // Verify that we have not specified a region set when using sigv4 algorithm
    if (isARegionSet(region)) {
      return absl::InvalidArgumentError(
          "SigV4 region string cannot contain wildcards or commas. Region sets "
          "can be specified when using signing_algorithm: AWS_SIGV4A.");
    }
    return std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
        config.service_name(), region, credentials_provider.value(), server_context, matcher_config,
        query_string, expiration_time);
  }
}

/**
 * Static registration for the AWS request signing filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsRequestSigningFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAwsRequestSigningFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
