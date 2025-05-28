#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

AwsIamAuthenticatorImpl::AwsIamAuthenticatorImpl(
    Server::Configuration::ServerFactoryContext& context, absl::string_view cache_name,
    absl::string_view service_name, absl::string_view region, uint16_t expiration_time,
    absl::optional<envoy::extensions::common::aws::v3::AwsCredentialProvider> credential_provider)
    : expiration_time_(expiration_time), cache_name_(std::string(cache_name)),
      service_name_(std::string(service_name)), region_(std::string(region)), context_(context) {

  Extensions::Common::Aws::CredentialsProviderChainSharedPtr credentials_provider_chain;

  if (credential_provider.has_value()) {
    credentials_provider_chain =
        std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
            context_.api(), context_, region_, credential_provider.value());
  } else {
    credentials_provider_chain =
        std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
            context_.api(), context_, region_);
  }

  signer_ = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      service_name_, region_, credentials_provider_chain, context_,
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true, expiration_time_);
}

AwsIamAuthenticatorSharedPtr AwsIamAuthenticatorFactory::initAwsIamAuthenticator(
    Server::Configuration::ServerFactoryContext& context,
    envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config) {

  return std::make_shared<AwsIamAuthenticatorImpl>(
      context, aws_iam_config.cache_name(), aws_iam_config.service_name(), aws_iam_config.region(),
      PROTOBUF_GET_SECONDS_OR_DEFAULT(aws_iam_config, expiration_time, 60),
      aws_iam_config.credential_provider());
}

std::string AwsIamAuthenticatorImpl::getAuthToken(std::string auth_user) {
  ENVOY_LOG(debug, "Generating new AWS IAM authentication token");
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(cache_name_);
  message.headers().setPath(fmt::format("/?Action=connect&User={}",
                                        Envoy::Http::Utility::PercentEncoding::encode(auth_user)));

  auto status = signer_->sign(message, true, region_);

  auth_token_ = cache_name_ + std::string(message.headers().getPathValue());
  auto query_params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(message.headers().getPathValue());

  query_params.overwrite(
      Envoy::Extensions::Common::Aws::SignatureQueryParameterValues::AmzSignature, "*****");
  if (query_params.getFirstValue(
          Envoy::Extensions::Common::Aws::SignatureQueryParameterValues::AmzSecurityToken)) {
    query_params.overwrite(
        Envoy::Extensions::Common::Aws::SignatureQueryParameterValues::AmzSecurityToken, "*****");
  }
  auto sanitised_query_string =
      query_params.replaceQueryString(Http::HeaderString(message.headers().getPathValue()));
  ENVOY_LOG(debug, "Generated authentication token (sanitised): {}{}", cache_name_,
            sanitised_query_string);
  return auth_token_;
}

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
