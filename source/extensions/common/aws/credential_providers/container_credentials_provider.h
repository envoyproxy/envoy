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

// #include "absl/strings/string_view.h"
#include "source/extensions/common/aws/cached_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] = "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
constexpr char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE";
constexpr char EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";
constexpr char EXPIRATION[] = "Expiration";
    
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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
