#include "source/extensions/grpc_credentials/aws_iam/config.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/aws_iam.pb.h"
#include "envoy/config/grpc_credential/v3/aws_iam.pb.validate.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/grpc/google_grpc_creds_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/region_provider_impl.h"
#include "source/extensions/common/aws/signer_impl.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace AwsIam {

std::shared_ptr<grpc::ChannelCredentials> AwsIamGrpcCredentialsFactory::getChannelCredentials(
    const envoy::config::core::v3::GrpcService& grpc_service_config, Api::Api& api) {

  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds =
      Grpc::CredsUtility::defaultSslChannelCredentials(grpc_service_config, api);

  std::shared_ptr<grpc::CallCredentials> call_creds;
  for (const auto& credential : google_grpc.call_credentials()) {
    switch (credential.credential_specifier_case()) {
    case envoy::config::core::v3::GrpcService::GoogleGrpc::CallCredentials::
        CredentialSpecifierCase::kFromPlugin: {
      if (credential.from_plugin().name() == "envoy.grpc_credentials.aws_iam") {
        AwsIamGrpcCredentialsFactory credentials_factory;
        // We don't deal with validation failures here at runtime today, see
        // https://github.com/envoyproxy/envoy/issues/8010.
        const Envoy::ProtobufTypes::MessagePtr config_message =
            Envoy::Config::Utility::translateToFactoryConfig(
                credential.from_plugin(), ProtobufMessage::getNullValidationVisitor(),
                credentials_factory);
        const auto& config = Envoy::MessageUtil::downcastAndValidate<
            const envoy::config::grpc_credential::v3::AwsIamConfig&>(
            *config_message, ProtobufMessage::getNullValidationVisitor());
        const auto region = getRegion(config);
        // TODO(suniltheta): Due to the reasons explained in
        // https://github.com/envoyproxy/envoy/issues/27586 this aws iam plugin is not able to
        // utilize http async client to fetch AWS credentials. For time being this is still using
        // libcurl to fetch the credentials. To fully get rid of curl, need to address the below
        // usage of AWS credentials common utils. Until then we are setting nullopt for server
        // factory context.
        auto credentials_provider = std::make_shared<Common::Aws::DefaultCredentialsProviderChain>(
            api, absl::nullopt /*Empty factory context*/, region,
            Common::Aws::Utility::fetchMetadata);
        auto signer = std::make_unique<Common::Aws::SignerImpl>(
            config.service_name(), region, credentials_provider, api.timeSource(),
            // TODO: extend API to allow specifying header exclusion. ref:
            // https://github.com/envoyproxy/envoy/pull/18998
            Common::Aws::AwsSigV4HeaderExclusionVector{});
        std::shared_ptr<grpc::CallCredentials> new_call_creds = grpc::MetadataCredentialsFromPlugin(
            std::make_unique<AwsIamHeaderAuthenticator>(std::move(signer)));
        if (call_creds == nullptr) {
          call_creds = new_call_creds;
        } else {
          call_creds = grpc::CompositeCallCredentials(call_creds, new_call_creds);
        }
      }
      break;
    }
    default:
      // unused credential types
      continue;
    }
  }

  if (call_creds != nullptr) {
    return grpc::CompositeChannelCredentials(creds, call_creds);
  }

  return creds;
}

std::string AwsIamGrpcCredentialsFactory::getRegion(
    const envoy::config::grpc_credential::v3::AwsIamConfig& config) {
  Common::Aws::RegionProviderPtr region_provider;
  if (!config.region().empty()) {
    region_provider = std::make_unique<Common::Aws::StaticRegionProvider>(config.region());
  } else {
    region_provider = std::make_unique<Common::Aws::EnvironmentRegionProvider>();
  }

  if (!region_provider->getRegion().has_value()) {
    throw EnvoyException("Could not determine AWS region. "
                         "If you are not running Envoy in EC2 or ECS, "
                         "provide the region in the plugin configuration.");
  }

  return *region_provider->getRegion();
}

grpc::Status
AwsIamHeaderAuthenticator::GetMetadata(grpc::string_ref service_url, grpc::string_ref method_name,
                                       const grpc::AuthContext&,
                                       std::multimap<grpc::string, grpc::string>* metadata) {

  auto message = buildMessageToSign(absl::string_view(service_url.data(), service_url.length()),
                                    absl::string_view(method_name.data(), method_name.length()));

  TRY_NEEDS_AUDIT { signer_->sign(message, false); }
  END_TRY catch (const EnvoyException& e) {
    return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
  }

  signedHeadersToMetadata(message.headers(), *metadata);

  return grpc::Status::OK;
}

Http::RequestMessageImpl
AwsIamHeaderAuthenticator::buildMessageToSign(absl::string_view service_url,
                                              absl::string_view method_name) {

  const auto uri = fmt::format("{}/{}", service_url, method_name);
  absl::string_view host;
  absl::string_view path;
  Http::Utility::extractHostPathFromUri(uri, host, path);

  Http::RequestMessageImpl message;
  message.headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message.headers().setHost(host);
  message.headers().setPath(path);

  return message;
}

void AwsIamHeaderAuthenticator::signedHeadersToMetadata(
    const Http::HeaderMap& headers, std::multimap<grpc::string, grpc::string>& metadata) {

  headers.iterate([&metadata](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    const auto& key = entry.key().getStringView();
    // Skip pseudo-headers
    if (key.empty() || key[0] == ':') {
      return Http::HeaderMap::Iterate::Continue;
    }
    metadata.emplace(key, entry.value().getStringView());
    return Http::HeaderMap::Iterate::Continue;
  });
}

REGISTER_FACTORY(AwsIamGrpcCredentialsFactory, Grpc::GoogleGrpcCredentialsFactory);

} // namespace AwsIam
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
