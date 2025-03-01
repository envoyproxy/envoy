#pragma once

// #include <list>
// #include <memory>
// #include <optional>
// #include <string>

// #include "envoy/api/api.h"
// #include "envoy/common/optref.h"
// #include "envoy/config/core/v3/base.pb.h"
// #include "envoy/event/timer.h"
// #include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
// #include "envoy/http/message.h"
// #include "envoy/server/factory_context.h"

// #include "source/common/common/lock_guard.h"
// #include "source/common/common/logger.h"
// #include "source/common/common/thread.h"
// #include "source/common/config/datasource.h"
// #include "source/common/init/target_impl.h"
// #include "source/common/protobuf/message_validator_impl.h"
// #include "source/common/protobuf/utility.h"
// #include "source/extensions/common/aws/aws_cluster_manager.h"
// #include "source/extensions/common/aws/credentials_provider.h"
// #include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

// #include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
/**
 * Retrieve AWS credentials from the instance metadata.
 *
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html#instance-metadata-security-credentials
 */
 class InstanceProfileCredentialsProvider : public MetadataCredentialsProviderBase,
 public Envoy::Singleton::Instance,
 public MetadataFetcher::MetadataReceiver {
public:
InstanceProfileCredentialsProvider(Api::Api& api, ServerFactoryContextOptRef context,
AwsClusterManagerOptRef aws_cluster_manager,
const CurlMetadataFetcher& fetch_metadata_using_curl,
CreateMetadataFetcherCb create_metadata_fetcher_cb,
MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
std::chrono::seconds initialization_timer,
absl::string_view cluster_name);

// Following functions are for MetadataFetcher::MetadataReceiver interface
void onMetadataSuccess(const std::string&& body) override;
void onMetadataError(Failure reason) override;
std::string providerName() override { return "InstanceProfileCredentialsProvider"; };

private:
bool needsRefresh() override;
void refresh() override;
void fetchInstanceRole(const std::string&& token, bool async = false);
void fetchInstanceRoleAsync(const std::string&& token) {
fetchInstanceRole(std::move(token), true);
}
void fetchCredentialFromInstanceRole(const std::string&& instance_role, const std::string&& token,
bool async = false);
void fetchCredentialFromInstanceRoleAsync(const std::string&& instance_role,
  const std::string&& token) {
fetchCredentialFromInstanceRole(std::move(instance_role), std::move(token), true);
}
void extractCredentials(const std::string&& credential_document_value, bool async = false);
void extractCredentialsAsync(const std::string&& credential_document_value) {
extractCredentials(std::move(credential_document_value), true);
}
};

    using InstanceProfileCredentialsProviderPtr = std::shared_ptr<InstanceProfileCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
