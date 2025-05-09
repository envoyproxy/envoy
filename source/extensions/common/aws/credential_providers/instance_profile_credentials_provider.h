#pragma once

#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char EC2_METADATA_HOST[] = "169.254.169.254:80";
constexpr char EC2_IMDS_TOKEN_RESOURCE[] = "/latest/api/token";
constexpr char EC2_IMDS_TOKEN_HEADER[] = "X-aws-ec2-metadata-token";
constexpr char EC2_IMDS_TOKEN_TTL_HEADER[] = "X-aws-ec2-metadata-token-ttl-seconds";
constexpr char EC2_IMDS_TOKEN_TTL_DEFAULT_VALUE[] = "21600";
constexpr char SECURITY_CREDENTIALS_PATH[] = "/latest/meta-data/iam/security-credentials";
constexpr char AWS_EC2_METADATA_DISABLED[] = "AWS_EC2_METADATA_DISABLED";
constexpr char EC2_METADATA_CLUSTER[] = "ec2_instance_metadata_server_internal";
/**
 * Retrieve AWS credentials from the instance metadata.
 *
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html#instance-metadata-security-credentials
 */
class InstanceProfileCredentialsProvider : public MetadataCredentialsProviderBase,
                                           public Envoy::Singleton::Instance,
                                           public MetadataFetcher::MetadataReceiver {
public:
  InstanceProfileCredentialsProvider(Api::Api& api,
                                     Server::Configuration::ServerFactoryContext& context,
                                     AwsClusterManagerPtr aws_cluster_manager,
                                     CreateMetadataFetcherCb create_metadata_fetcher_cb,
                                     MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                                     std::chrono::seconds initialization_timer,
                                     absl::string_view cluster_name);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;
  std::string providerName() override { return "InstanceProfileCredentialsProvider"; };

private:
  void refresh() override;
  void fetchInstanceRoleAsync(const std::string&& token);
  void fetchCredentialFromInstanceRoleAsync(const std::string&& instance_role,
                                            const std::string&& token);
  void extractCredentialsAsync(const std::string&& credential_document_value);
};

using InstanceProfileCredentialsProviderPtr = std::shared_ptr<InstanceProfileCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
