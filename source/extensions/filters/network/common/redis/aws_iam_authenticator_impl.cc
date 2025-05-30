#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/region_provider_impl.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

absl::optional<AwsIamAuthenticatorSharedPtr> AwsIamAuthenticatorFactory::authenticator_handle_;

SINGLETON_MANAGER_REGISTRATION(aws_iam_authenticator);

AwsIamAuthenticatorImpl::AwsIamAuthenticatorImpl(absl::string_view cache_name,
                                                 absl::string_view region,
                                                 Envoy::Extensions::Common::Aws::SignerPtr signer)
    : signer_(std::move(signer)), cache_name_(std::string(cache_name)), region_(region) {}

void AwsIamAuthenticatorImpl::shutDown() {
  ENVOY_LOG_MISC(debug, "****** INSIDE SHUTDOWN ******");
    signer_.reset();
  }

absl::optional<AwsIamAuthenticatorSharedPtr> AwsIamAuthenticatorFactory::initAwsIamAuthenticator(
    Server::Configuration::ServerFactoryContext& context,
    envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config) {

  context.lifecycleNotifier().registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                                                       [&](Event::PostCb) { 
                                                          ENVOY_LOG_MISC(debug, "****** INSIDE PRE SHUTDOWN ******");
                                                        if(authenticator_handle_.has_value())
                                                        {
                                                          authenticator_handle_.value()->shutDown();
                                                        }
                                                      });

  absl::StatusOr<Extensions::Common::Aws::CredentialsProviderChainSharedPtr>
      credentials_provider_chain;

  std::string region;

  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider credential_file_config = {};
  if (aws_iam_config.has_credential_provider()) {
    if (aws_iam_config.credential_provider().has_credentials_file_provider()) {
      credential_file_config = aws_iam_config.credential_provider().credentials_file_provider();
    }
  }

  if (aws_iam_config.region().empty()) {
    auto region_provider =
        std::make_shared<Extensions::Common::Aws::RegionProviderChain>(credential_file_config);
    absl::optional<std::string> regionOpt;
    regionOpt = region_provider->getRegion();
    if (!regionOpt.has_value()) {
      ENVOY_LOG(error, "AWS region is not set in xDS configuration and failed to retrieve from "
                       "environment variable or AWS profile/config files.");
      return absl::nullopt;
    }
    region = regionOpt.value();
  }
  else {
    region = aws_iam_config.region();
  }

  if (aws_iam_config.has_credential_provider()) {
    credentials_provider_chain =
        Extensions::Common::Aws::CommonCredentialsProviderChain::customCredentialsProviderChain(
            context, region, aws_iam_config.credential_provider());
  } else {
    credentials_provider_chain =
        Extensions::Common::Aws::CommonCredentialsProviderChain::defaultCredentialsProviderChain(
            context, region);
  }

  if (!credentials_provider_chain.ok()) {
    ENVOY_LOG(error, "Failed to initialize AWS credentials provider chain: {}",
              credentials_provider_chain.status().message());
    return absl::nullopt;
  }
  // auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
  //     aws_iam_config.service_name().empty() ? DEFAULT_SERVICE_NAME : aws_iam_config.service_name(),
  //     region, credentials_provider_chain.value(), context,
  //     Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true,
  //     PROTOBUF_GET_SECONDS_OR_DEFAULT(aws_iam_config, expiration_time, 60));

        authenticator_handle_ = context.singletonManager().getTyped<AwsIamAuthenticatorImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(aws_iam_authenticator),
      [&aws_iam_config, credentials_provider_chain, &context, &region]()  { 
          auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      aws_iam_config.service_name().empty() ? DEFAULT_SERVICE_NAME : aws_iam_config.service_name(),
      region, credentials_provider_chain.value(), context,
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true,
      PROTOBUF_GET_SECONDS_OR_DEFAULT(aws_iam_config, expiration_time, 60));

        return std::make_shared<AwsIamAuthenticatorImpl>(aws_iam_config.cache_name(),  region, std::move(signer)); 
      
      }, false);

      return authenticator_handle_;

  // aws_cluster_manager_ =
  //     context.singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
  //         SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
  //         [&context] {
  //           return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(context);
  //         },
  //         true);

  // return std::make_shared<AwsIamAuthenticatorImpl>( aws_iam_config.cache_name(),  region, std::move(signer) );
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
