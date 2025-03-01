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
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {


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
AwsClusterManagerOptRef aws_cluster_manager, absl::string_view cluster_name,
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
absl::optional<Config::DataSource::DataSourceProviderPtr> web_identity_data_source_provider_;
const std::string role_arn_;
const std::string role_session_name_;

bool needsRefresh() override;
void refresh() override;
void extractCredentials(const std::string&& credential_document_value);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
