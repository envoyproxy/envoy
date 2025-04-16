#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

CredentialsFileCredentialsProvider::CredentialsFileCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
        credential_file_config)
    : context_(context), profile_("") {

  if (credential_file_config.has_credentials_data_source()) {
    auto provider_or_error_ = Config::DataSource::DataSourceProvider::create(
        credential_file_config.credentials_data_source(), context.mainThreadDispatcher(),
        context.threadLocal(), context.api(), false, 4096);
    if (provider_or_error_.ok()) {
      credential_file_data_source_provider_ = std::move(provider_or_error_.value());
      if (credential_file_config.credentials_data_source().has_watched_directory()) {
        has_watched_directory_ = true;
      }
    } else {
      ENVOY_LOG(info, "Invalid credential file data source");
      credential_file_data_source_provider_.reset();
    }
  }
  if (!credential_file_config.profile().empty()) {
    profile_ = credential_file_config.profile();
  }
}

bool CredentialsFileCredentialsProvider::needsRefresh() {
  return has_watched_directory_
             ? true
             : context_.api().timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

void CredentialsFileCredentialsProvider::refresh() {
  auto profile = profile_.empty() ? Utility::getCredentialProfileName() : profile_;

  ENVOY_LOG(debug, "Getting AWS credentials from the credentials file");

  std::string credential_file_data, credential_file_path;

  // Use data source if provided, otherwise read from default AWS credential file path
  if (credential_file_data_source_provider_.has_value()) {
    credential_file_data = credential_file_data_source_provider_.value()->data();
    credential_file_path = "<config datasource>";
  } else {
    credential_file_path = Utility::getCredentialFilePath();
    auto credential_file = context_.api().fileSystem().fileReadToEnd(credential_file_path);
    if (credential_file.ok()) {
      credential_file_data = credential_file.value();
    } else {
      ENVOY_LOG(debug, "Unable to read from credential file {}", credential_file_path);
      // Update last_updated_ now so that even if this function returns before successfully
      // extracting credentials, this function won't be called again until after the
      // REFRESH_INTERVAL. This prevents envoy from attempting and failing to read the credentials
      // file on every request if there are errors extracting credentials from it (e.g. if the
      // credentials file doesn't exist).
      last_updated_ = context_.api().timeSource().systemTime();
      return;
    }
  }
  ENVOY_LOG(debug, "Credentials file path = {}, profile name = {}", credential_file_path, profile);

  extractCredentials(credential_file_data.data(), profile);
}

void CredentialsFileCredentialsProvider::extractCredentials(absl::string_view credentials_string,
                                                            absl::string_view profile) {

  std::string access_key_id, secret_access_key, session_token;

  // TODO: @nbaws optimize out this flat hash map creation

  absl::flat_hash_map<std::string, std::string> elements = {
      {AWS_ACCESS_KEY_ID, ""}, {AWS_SECRET_ACCESS_KEY, ""}, {AWS_SESSION_TOKEN, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;
  Utility::resolveProfileElementsFromString(credentials_string.data(), profile.data(), elements);
  // if profile file fails to load, or these elements are not found in the profile, their values
  // will remain blank when retrieving them from the hash map
  access_key_id = elements.find(AWS_ACCESS_KEY_ID)->second;
  secret_access_key = elements.find(AWS_SECRET_ACCESS_KEY)->second;
  session_token = elements.find(AWS_SESSION_TOKEN)->second;

  if (access_key_id.empty() || secret_access_key.empty()) {
    // Return empty credentials if we're unable to retrieve from profile
    cached_credentials_ = Credentials();
  } else {
    ENVOY_LOG(debug, "Found following AWS credentials for profile '{}': {}={}, {}={}, {}={}",
              profile, AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
              secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
              session_token.empty() ? "" : "*****");

    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  last_updated_ = context_.api().timeSource().systemTime();
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
