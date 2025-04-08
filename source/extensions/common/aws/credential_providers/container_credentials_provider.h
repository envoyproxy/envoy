#pragma once
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] = "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
constexpr char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE";
constexpr char CONTAINER_EXPIRATION[] = "Expiration";
constexpr char CONTAINER_METADATA_HOST[] = "169.254.170.2:80";
constexpr char CONTAINER_METADATA_CLUSTER[] = "ecs_task_metadata_server_internal";

/**
 * Retrieve AWS credentials from the task metadata.
 *
 * https://docs.aws.amazon.com/AmazonECS/latest/developerguide/task-iam-roles.html#enable_task_iam_roles
 */
class ContainerCredentialsProvider : public MetadataCredentialsProviderBase,
                                     public Envoy::Singleton::Instance,
                                     public MetadataFetcher::MetadataReceiver {
public:
  ContainerCredentialsProvider(Api::Api& api, ServerFactoryContextOptRef context,
                               AwsClusterManagerOptRef aws_cluster_manager,
                               const CurlMetadataFetcher& fetch_metadata_using_curl,
                               CreateMetadataFetcherCb create_metadata_fetcher_cb,
                               absl::string_view credential_uri,
                               MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                               std::chrono::seconds initialization_timer,
                               absl::string_view authorization_token,
                               absl::string_view cluster_name);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;
  std::string providerName() override { return "ContainerCredentialsProvider"; };

private:
  const std::string credential_uri_;
  const std::string authorization_token_;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(const std::string&& credential_document_value);
};

using ContainerCredentialsProviderPtr = std::shared_ptr<ContainerCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
