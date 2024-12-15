#include "source/extensions/filters/http/aws_request_signing/config.h"

#include <iterator>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/region_provider_impl.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "source/extensions/common/aws/sigv4a_signer_impl.h"
#include "source/extensions/common/aws/utility.h"
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

  absl::StatusOr<Envoy::Extensions::Common::Aws::CredentialsProviderSharedPtr>
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
      credentials_provider = std::make_shared<Extensions::Common::Aws::InlineCredentialProvider>(
          inline_credential.access_key_id(), inline_credential.secret_access_key(),
          inline_credential.session_token());
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
              server_context.api(), makeOptRef(server_context), server_context.singletonManager(),
              region, nullptr, credential_provider_config);
    }
  } else {
    // No credential provider settings provided, so make the default credentials provider chain
    credentials_provider =
        std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
            server_context.api(), makeOptRef(server_context), server_context.singletonManager(),
            region, nullptr, credential_provider_config);
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
    signer = std::make_unique<Extensions::Common::Aws::SigV4ASignerImpl>(
        config.service_name(), region, credentials_provider.value(), server_context, matcher_config,
        query_string, expiration_time);
  } else {
    // Verify that we have not specified a region set when using sigv4 algorithm
    if (isARegionSet(region)) {
      return absl::InvalidArgumentError(
          "SigV4 region string cannot contain wildcards or commas. Region sets "
          "can be specified when using signing_algorithm: AWS_SIGV4A.");
    }
    signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
        config.service_name(), region, credentials_provider.value(), server_context, matcher_config,
        query_string, expiration_time);
  }

  auto filter_config =
      std::make_shared<FilterConfigImpl>(std::move(signer), stats_prefix, dual_info.scope,
                                         config.host_rewrite(), config.use_unsigned_payload());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamDecoderFilter(filter);
  };
}

// TODO: @nbaws remove duplication from above

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
AwsRequestSigningFilterFactory::createRouteSpecificFilterConfigTyped(
    const AwsRequestSigningProtoPerRouteConfig& per_route_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {

  std::string region = per_route_config.aws_request_signing().region();

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  // If we have an overriding credential provider configuration, read it here
  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider credential_file_config = {};
  if (per_route_config.aws_request_signing().has_credential_provider()) {
    if (per_route_config.aws_request_signing()
            .credential_provider()
            .has_credentials_file_provider()) {
      credential_file_config =
          per_route_config.aws_request_signing().credential_provider().credentials_file_provider();
    }
  }

  if (region.empty()) {
    auto region_provider = std::make_shared<Extensions::Common::Aws::RegionProviderChain>();
    absl::optional<std::string> regionOpt;
    if (per_route_config.aws_request_signing().signing_algorithm() ==
        AwsRequestSigning_SigningAlgorithm_AWS_SIGV4A) {
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

  absl::StatusOr<Envoy::Extensions::Common::Aws::CredentialsProviderSharedPtr>
      credentials_provider =
          absl::InvalidArgumentError("No credentials provider settings configured.");

  bool has_credential_provider_settings =
      per_route_config.aws_request_signing().has_credential_provider() &&
      (per_route_config.aws_request_signing()
           .credential_provider()
           .has_assume_role_with_web_identity_provider() ||
       per_route_config.aws_request_signing()
           .credential_provider()
           .has_credentials_file_provider());

  if (per_route_config.aws_request_signing().has_credential_provider()) {
    if (per_route_config.aws_request_signing().credential_provider().has_inline_credential()) {
      const auto& inline_credential =
          per_route_config.aws_request_signing().credential_provider().inline_credential();
      credentials_provider = std::make_shared<Extensions::Common::Aws::InlineCredentialProvider>(
          inline_credential.access_key_id(), inline_credential.secret_access_key(),
          inline_credential.session_token());
    }

    if (per_route_config.aws_request_signing()
            .credential_provider()
            .custom_credential_provider_chain()) {
      // Custom credential provider chain
      if (has_credential_provider_settings) {
        credentials_provider =
            std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
                server_context, region,
                per_route_config.aws_request_signing().credential_provider());
      }
    } else {
      // Override default credential provider chain settings with any provided settings
      if (has_credential_provider_settings) {
        credential_provider_config = per_route_config.aws_request_signing().credential_provider();
      }
      credentials_provider =
          std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
              server_context.api(), makeOptRef(server_context), server_context.singletonManager(),
              region, nullptr, credential_provider_config);
    }
  } else {
    // No credential provider settings provided, so make the default credentials provider chain
    credentials_provider =
        std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
            server_context.api(), makeOptRef(server_context), server_context.singletonManager(),
            region, nullptr, credential_provider_config);
  }

  if (!credentials_provider.ok()) {
    return absl::InvalidArgumentError(std::string(credentials_provider.status().message()));
  }

  const auto matcher_config = Extensions::Common::Aws::AwsSigningHeaderExclusionVector(
      per_route_config.aws_request_signing().match_excluded_headers().begin(),
      per_route_config.aws_request_signing().match_excluded_headers().end());

  const bool query_string = per_route_config.aws_request_signing().has_query_string();

  const uint16_t expiration_time = PROTOBUF_GET_SECONDS_OR_DEFAULT(
      per_route_config.aws_request_signing().query_string(), expiration_time, 5);

  std::unique_ptr<Extensions::Common::Aws::Signer> signer;

  if (per_route_config.aws_request_signing().signing_algorithm() ==
      AwsRequestSigning_SigningAlgorithm_AWS_SIGV4A) {
    signer = std::make_unique<Extensions::Common::Aws::SigV4ASignerImpl>(
        per_route_config.aws_request_signing().service_name(), region, credentials_provider.value(),
        server_context, matcher_config, query_string, expiration_time);
  } else {
    // Verify that we have not specified a region set when using sigv4 algorithm
    if (isARegionSet(region)) {
      return absl::InvalidArgumentError(
          "SigV4 region string cannot contain wildcards or commas. Region sets "
          "can be specified when using signing_algorithm: AWS_SIGV4A.");
    }
    signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
        per_route_config.aws_request_signing().service_name(), region, credentials_provider.value(),
        server_context, matcher_config, query_string, expiration_time);
  }

  return std::make_shared<const FilterConfigImpl>(
      std::move(signer), per_route_config.stat_prefix(), server_context.scope(),
      per_route_config.aws_request_signing().host_rewrite(),
      per_route_config.aws_request_signing().use_unsigned_payload());
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
