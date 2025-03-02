#pragma once

// #include <list>
// #include <memory>
// #include <optional>
// #include <string>

// #include "envoy/api/api.h"
// #include "envoy/common/optref.h"
// #include "envoy/config/core/v3/base.pb.h"
// #include "envoy/event/timer.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

// #include "envoy/http/message.h"
// #include "envoy/server/factory_context.h"

// #include "source/common/common/lock_guard.h"
// #include "source/common/common/logger.h"
// #include "source/common/common/thread.h"
#include "source/common/config/datasource.h"
// #include "source/common/init/target_impl.h"
// #include "source/common/protobuf/message_validator_impl.h"
// #include "source/common/protobuf/utility.h"
// #include "source/extensions/common/aws/aws_cluster_manager.h"
// #include "source/extensions/common/aws/credentials_provider.h"
// #include "source/extensions/common/aws/metadata_fetcher.h"

// #include "absl/strings/string_view.h"
#include "source/extensions/common/aws/cached_credentials_provider_base.h"
namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Retrieve AWS credentials from the credentials file.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html
 */
class CredentialsFileCredentialsProvider : public CachedCredentialsProviderBase {
public:
  CredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config = {});
  std::string providerName() override { return "CredentialsFileCredentialsProvider"; };

private:
  Server::Configuration::ServerFactoryContext& context_;
  std::string profile_;
  absl::optional<Config::DataSource::DataSourceProviderPtr> credential_file_data_source_provider_;
  bool has_watched_directory_ = false;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(absl::string_view credentials_string, absl::string_view profile);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
