#include "source/extensions/common/aws/credentials_provider_impl.h"

#include <fstream>

#include "envoy/common/exception.h"

#include "source/common/common/lock_guard.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

namespace {

constexpr char AWS_ACCESS_KEY_ID[] = "AWS_ACCESS_KEY_ID";
constexpr char AWS_SECRET_ACCESS_KEY[] = "AWS_SECRET_ACCESS_KEY";
constexpr char AWS_SESSION_TOKEN[] = "AWS_SESSION_TOKEN";

constexpr char ACCESS_KEY_ID[] = "AccessKeyId";
constexpr char SECRET_ACCESS_KEY[] = "SecretAccessKey";
constexpr char TOKEN[] = "Token";
constexpr char EXPIRATION[] = "Expiration";
constexpr char EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";
constexpr char TRUE[] = "true";

constexpr char AWS_SHARED_CREDENTIALS_FILE[] = "AWS_SHARED_CREDENTIALS_FILE";
constexpr char AWS_PROFILE[] = "AWS_PROFILE";
constexpr char DEFAULT_AWS_SHARED_CREDENTIALS_FILE[] = "~/.aws/credentials";
constexpr char DEFAULT_AWS_PROFILE[] = "default";

constexpr char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] = "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
constexpr char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
constexpr char AWS_EC2_METADATA_DISABLED[] = "AWS_EC2_METADATA_DISABLED";

constexpr std::chrono::hours REFRESH_INTERVAL{1};
constexpr std::chrono::seconds REFRESH_GRACE_PERIOD{5};
constexpr char EC2_METADATA_HOST[] = "169.254.169.254:80";
constexpr char CONTAINER_METADATA_HOST[] = "169.254.170.2:80";
constexpr char EC2_IMDS_TOKEN_RESOURCE[] = "/latest/api/token";
constexpr char EC2_IMDS_TOKEN_HEADER[] = "X-aws-ec2-metadata-token";
constexpr char EC2_IMDS_TOKEN_TTL_HEADER[] = "X-aws-ec2-metadata-token-ttl-seconds";
constexpr char EC2_IMDS_TOKEN_TTL_DEFAULT_VALUE[] = "21600";
constexpr char SECURITY_CREDENTIALS_PATH[] = "/latest/meta-data/iam/security-credentials";

} // namespace

Credentials EnvironmentCredentialsProvider::getCredentials() {
  ENVOY_LOG(debug, "Getting AWS credentials from the environment");

  const auto access_key_id = absl::NullSafeStringView(std::getenv(AWS_ACCESS_KEY_ID));
  if (access_key_id.empty()) {
    return Credentials();
  }

  const auto secret_access_key = absl::NullSafeStringView(std::getenv(AWS_SECRET_ACCESS_KEY));
  const auto session_token = absl::NullSafeStringView(std::getenv(AWS_SESSION_TOKEN));

  ENVOY_LOG(debug, "Found following AWS credentials in the environment: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  return Credentials(access_key_id, secret_access_key, session_token);
}

void CachedCredentialsProviderBase::refreshIfNeeded() {
  const Thread::LockGuard lock(lock_);
  if (needsRefresh()) {
    refresh();
  }
}

bool CredentialsFileCredentialsProvider::needsRefresh() {
  return api_.timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

std::string getEnvironmentVariableOrDefault(const std::string& variable_name,
                                            const std::string& default_value) {
  const char* value = getenv(variable_name.c_str());
  return (value != nullptr) && (value[0] != '\0') ? value : default_value;
}

void CredentialsFileCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the credentials file");

  const auto credentials_file = getEnvironmentVariableOrDefault(
      AWS_SHARED_CREDENTIALS_FILE, DEFAULT_AWS_SHARED_CREDENTIALS_FILE);
  const auto profile = getEnvironmentVariableOrDefault(AWS_PROFILE, DEFAULT_AWS_PROFILE);

  extractCredentials(credentials_file, profile);
}

void CredentialsFileCredentialsProvider::extractCredentials(const std::string& credentials_file,
                                                            const std::string& profile) {
  // Update last_updated_ now so that even if this function returns before successfully
  // extracting credentials, this function won't be called again until after the REFRESH_INTERVAL.
  // This prevents envoy from attempting and failing to read the credentials file on every request
  // if there are errors extracting credentials from it (e.g. if the credentials file doesn't
  // exist).
  last_updated_ = api_.timeSource().systemTime();

  std::ifstream file(credentials_file);
  if (!file) {
    ENVOY_LOG(debug, "Error opening credentials file {}", credentials_file);
    return;
  }

  std::string access_key_id, secret_access_key, session_token;
  const auto profile_start = absl::StrFormat("[%s]", profile);

  bool found_profile = false;
  std::string line;
  while (std::getline(file, line)) {
    line = std::string(StringUtil::trim(line));
    if (line.empty()) {
      continue;
    }

    if (line == profile_start) {
      found_profile = true;
      continue;
    }

    if (found_profile) {
      // Stop reading once we find the start of the next profile.
      if (absl::StartsWith(line, "[")) {
        break;
      }

      std::vector<std::string> parts = absl::StrSplit(line, absl::MaxSplits('=', 1));
      if (parts.size() == 2) {
        const auto key = StringUtil::toUpper(StringUtil::trim(parts[0]));
        const auto val = StringUtil::trim(parts[1]);
        if (key == AWS_ACCESS_KEY_ID) {
          access_key_id = val;
        } else if (key == AWS_SECRET_ACCESS_KEY) {
          secret_access_key = val;
        } else if (key == AWS_SESSION_TOKEN) {
          session_token = val;
        }
      }
    }
  }

  ENVOY_LOG(debug, "Found following AWS credentials for profile '{}' in {}: {}={}, {}={}, {}={}",
            profile, credentials_file, AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  last_updated_ = api_.timeSource().systemTime();
}

bool InstanceProfileCredentialsProvider::needsRefresh() {
  return api_.timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

void InstanceProfileCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the EC2MetadataService");

  // First request for a session TOKEN so that we can call EC2MetadataService securely.
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configuring-instance-metadata-service.html
  Http::RequestMessageImpl token_req_message;
  token_req_message.headers().setMethod(Http::Headers::get().MethodValues.Put);
  token_req_message.headers().setHost(EC2_METADATA_HOST);
  token_req_message.headers().setPath(EC2_IMDS_TOKEN_RESOURCE);
  token_req_message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_TTL_HEADER),
                                      EC2_IMDS_TOKEN_TTL_DEFAULT_VALUE);
  const auto token_string = metadata_fetcher_(token_req_message);
  if (token_string) {
    ENVOY_LOG(debug, "Obtained token to make secure call to EC2MetadataService");
    fetchInstanceRole(token_string.value());
  } else {
    ENVOY_LOG(warn, "Failed to get token from EC2MetadataService, falling back to less secure way");
    fetchInstanceRole("");
  }
}

void InstanceProfileCredentialsProvider::fetchInstanceRole(const std::string& token_string) {
  // Discover the Role of this instance.
  Http::RequestMessageImpl message;
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(EC2_METADATA_HOST);
  message.headers().setPath(SECURITY_CREDENTIALS_PATH);
  if (!token_string.empty()) {
    message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_HEADER),
                              StringUtil::trim(token_string));
  }
  const auto instance_role_string = metadata_fetcher_(message);
  if (!instance_role_string) {
    ENVOY_LOG(error, "Could not retrieve credentials listing from the EC2MetadataService");
    return;
  }
  fetchCredentialFromInstanceRole(instance_role_string.value(), token_string);
}

void InstanceProfileCredentialsProvider::fetchCredentialFromInstanceRole(
    const std::string& instance_role, const std::string& token_string) {
  if (instance_role.empty()) {
    return;
  }
  const auto instance_role_list = StringUtil::splitToken(StringUtil::trim(instance_role), "\n");
  if (instance_role_list.empty()) {
    ENVOY_LOG(error, "No AWS credentials were found in the EC2MetadataService");
    return;
  }
  ENVOY_LOG(debug, "AWS credentials list:\n{}", instance_role);

  // Only one Role can be associated with an instance:
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html
  const auto credential_path =
      std::string(SECURITY_CREDENTIALS_PATH) + "/" +
      std::string(instance_role_list[0].data(), instance_role_list[0].size());
  ENVOY_LOG(debug, "AWS credentials path: {}", credential_path);

  // Then fetch and parse the credentials
  Http::RequestMessageImpl message;
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(EC2_METADATA_HOST);
  message.headers().setPath(credential_path);
  if (!token_string.empty()) {
    message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_HEADER),
                              StringUtil::trim(token_string));
  }
  const auto credential_document = metadata_fetcher_(message);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the EC2MetadataService");
    return;
  }
  extractCredentials(credential_document.value());
}

void InstanceProfileCredentialsProvider::extractCredentials(
    const std::string& credential_document_value) {
  if (credential_document_value.empty()) {
    return;
  }
  Json::ObjectSharedPtr document_json;
  TRY_NEEDS_AUDIT { document_json = Json::Factory::loadFromString(credential_document_value); }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document: {}", e.what());
    return;
  }

  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  const auto session_token = document_json->getString(TOKEN, "");

  ENVOY_LOG(debug,
            "Obtained following AWS credentials from the EC2MetadataService: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  last_updated_ = api_.timeSource().systemTime();
}

bool TaskRoleCredentialsProvider::needsRefresh() {
  const auto now = api_.timeSource().systemTime();
  return (now - last_updated_ > REFRESH_INTERVAL) ||
         (expiration_time_ - now < REFRESH_GRACE_PERIOD);
}

void TaskRoleCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the task role at URI: {}", credential_uri_);

  absl::string_view host;
  absl::string_view path;
  Http::Utility::extractHostPathFromUri(credential_uri_, host, path);

  Http::RequestMessageImpl message;
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(host);
  message.headers().setPath(path);
  message.headers().setCopy(Http::CustomHeaders::get().Authorization, authorization_token_);
  const auto credential_document = metadata_fetcher_(message);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the task role");
    return;
  }
  extractCredentials(credential_document.value());
}

void TaskRoleCredentialsProvider::extractCredentials(const std::string& credential_document_value) {
  if (credential_document_value.empty()) {
    return;
  }
  Json::ObjectSharedPtr document_json;
  TRY_NEEDS_AUDIT { document_json = Json::Factory::loadFromString(credential_document_value); }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from the task role: {}", e.what());
    return;
  }

  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  const auto session_token = document_json->getString(TOKEN, "");

  ENVOY_LOG(debug, "Found following AWS credentials in the task role: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  const auto expiration_str = document_json->getString(EXPIRATION, "");
  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "Task role AWS credentials expiration time: {}", expiration_str);
      expiration_time_ = absl::ToChronoTime(expiration_time);
    }
  }

  last_updated_ = api_.timeSource().systemTime();
  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
}

Credentials CredentialsProviderChain::getCredentials() {
  for (auto& provider : providers_) {
    const auto credentials = provider->getCredentials();
    if (credentials.accessKeyId() && credentials.secretAccessKey()) {
      return credentials;
    }
  }

  ENVOY_LOG(debug, "No AWS credentials found, using anonymous credentials");
  return Credentials();
}

DefaultCredentialsProviderChain::DefaultCredentialsProviderChain(
    Api::Api& api, const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher,
    const CredentialsProviderChainFactories& factories) {
  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_aws_credentials_file")) {
    ENVOY_LOG(debug, "Using credentials file credentials provider");
    add(factories.createCredentialsFileCredentialsProvider(api));
  } else {
    ENVOY_LOG(debug, "Not using credential file credentials provider because it is not enabled");
  }

  const auto relative_uri =
      absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI));
  const auto full_uri = absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI));
  const auto metadata_disabled = absl::NullSafeStringView(std::getenv(AWS_EC2_METADATA_DISABLED));

  if (!relative_uri.empty()) {
    const auto uri = absl::StrCat(CONTAINER_METADATA_HOST, relative_uri);
    ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", uri);
    add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, uri));
  } else if (!full_uri.empty()) {
    const auto authorization_token =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
    if (!authorization_token.empty()) {
      ENVOY_LOG(debug,
                "Using task role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri,
                                                      authorization_token));
    } else {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri));
    }
  } else if (metadata_disabled != TRUE) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(api, metadata_fetcher));
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
