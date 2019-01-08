#include "extensions/grpc_credentials/aws_iam/config.h"

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v2alpha/aws_iam.pb.validate.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/aws/credentials_provider_impl.h"
#include "common/aws/region_provider_impl.h"
#include "common/aws/signer_impl.h"
#include "common/config/utility.h"
#include "common/grpc/google_grpc_creds_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace AwsIam {

std::shared_ptr<grpc::ChannelCredentials> AwsIamGrpcCredentialsFactory::getChannelCredentials(
    const envoy::api::v2::core::GrpcService& grpc_service_config,
    Grpc::GoogleGrpcCredentialsFactoryContext& context) {
  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds =
      Grpc::CredsUtility::defaultSslChannelCredentials(grpc_service_config);
  std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
  for (const auto& credential : google_grpc.call_credentials()) {
    switch (credential.credential_specifier_case()) {
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kFromPlugin: {
      if (credential.from_plugin().name() == GrpcCredentialsNames::get().AwsIam) {
        AwsIamGrpcCredentialsFactory credentials_factory;
        const Envoy::ProtobufTypes::MessagePtr config_message =
            Envoy::Config::Utility::translateToFactoryConfig(credential.from_plugin(),
                                                             credentials_factory);
        const auto& config = Envoy::MessageUtil::downcastAndValidate<
            const envoy::config::grpc_credential::v2alpha::AwsIamConfig&>(*config_message);
        Aws::Auth::RegionProviderSharedPtr region_provider;
        if (!config.region().empty()) {
          region_provider = std::make_shared<Aws::Auth::StaticRegionProvider>(config.region());
        } else {
          region_provider = std::make_shared<Aws::Auth::EnvironmentRegionProvider>();
        }
        auto credentials_provider = std::make_shared<Aws::Auth::DefaultCredentialsProviderChain>(
            context.api(), context.timeSystem());
        auto auth_signer = std::make_shared<Aws::Auth::SignerImpl>(
            config.service_name(), credentials_provider, region_provider, context.timeSystem());
        std::shared_ptr<grpc::CallCredentials> new_call_creds =
            grpc::MetadataCredentialsFromPlugin(std::make_unique<AwsIamAuthenticator>(auth_signer));
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

grpc::Status AwsIamAuthenticator::GetMetadata(grpc::string_ref service_url,
                                              grpc::string_ref method_name,
                                              const grpc::AuthContext&,
                                              std::multimap<grpc::string, grpc::string>* metadata) {
  const std::string uri(std::string(service_url.data(), service_url.size()) + "/" +
                        std::string(method_name.data(), method_name.size()));
  absl::string_view host;
  absl::string_view path;
  Http::Utility::extractHostPathFromUri(uri, host, path);
  Http::RequestMessageImpl message;
  message.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
  message.headers().insertHost().value(host);
  message.headers().insertPath().value(path);
  try {
    signer_->sign(message);
  } catch (const EnvoyException& e) {
    return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
  }
  // Copy back whatever headers were added
  message.headers().iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        auto* md = static_cast<std::multimap<grpc::string, grpc::string>*>(context);
        const auto& key = entry.key().getStringView();
        // Skip pseudo-headers
        if (key.empty() || key[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        md->emplace(entry.key().c_str(), entry.value().c_str());
        return Http::HeaderMap::Iterate::Continue;
      },
      metadata);
  return grpc::Status::OK;
}

static Registry::RegisterFactory<AwsIamGrpcCredentialsFactory, Grpc::GoogleGrpcCredentialsFactory>
    aws_iam_google_grpc_credentials_registered_;

} // namespace AwsIam
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
