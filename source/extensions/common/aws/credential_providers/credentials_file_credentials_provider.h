#pragma once
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/config/datasource.h"
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
  absl::optional<Config::DataSource::DataSourceProviderPtr<std::string>>
      credential_file_data_source_provider_;
  bool has_watched_directory_ = false;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(absl::string_view credentials_string, absl::string_view profile);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
