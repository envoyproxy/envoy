#pragma once
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/config/datasource.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char WEB_IDENTITY_RESPONSE_ELEMENT[] = "AssumeRoleWithWebIdentityResponse";
constexpr char WEB_IDENTITY_RESULT_ELEMENT[] = "AssumeRoleWithWebIdentityResult";
constexpr char AWS_WEB_IDENTITY_TOKEN_FILE[] = "AWS_WEB_IDENTITY_TOKEN_FILE";
constexpr char AWS_ROLE_ARN[] = "AWS_ROLE_ARN";
constexpr char STS_TOKEN_CLUSTER[] = "sts_token_service_internal";
constexpr char AWS_ROLE_SESSION_NAME[] = "AWS_ROLE_SESSION_NAME";

/**
 * Retrieve AWS credentials from Security Token Service using a web identity token (e.g. OAuth,
 * OpenID)
 */
class WebIdentityCredentialsProvider : public MetadataCredentialsProviderBase,
                                       public MetadataFetcher::MetadataReceiver {
public:
  // token and token_file_path are mutually exclusive. If token is not empty, token_file_path is
  // not used, and vice versa.
  WebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;
  std::string providerName() override { return "WebIdentityCredentialsProvider"; };

private:
  const std::string sts_endpoint_;
  absl::optional<Config::DataSource::DataSourceProviderPtr<std::string>>
      web_identity_data_source_provider_;
  const std::string role_arn_;
  const std::string role_session_name_;

  void refresh() override;
  void extractCredentials(const std::string&& credential_document_value);
};

using WebIdentityCredentialsProviderPtr = std::shared_ptr<WebIdentityCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
