#include "extensions/filters/http/common/aws/credentials_provider_impl.h"

#include "envoy/common/exception.h"

#include "common/common/lock_guard.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

static const char AWS_ACCESS_KEY_ID[] = "AWS_ACCESS_KEY_ID";
static const char AWS_SECRET_ACCESS_KEY[] = "AWS_SECRET_ACCESS_KEY";
static const char AWS_SESSION_TOKEN[] = "AWS_SESSION_TOKEN";

static const char ACCESS_KEY_ID[] = "AccessKeyId";
static const char SECRET_ACCESS_KEY[] = "SecretAccessKey";
static const char TOKEN[] = "Token";
static const char EXPIRATION[] = "Expiration";
static const char EXPIRATION_FORMAT[] = "%E4Y%m%dT%H%M%S%z";
static const char TRUE[] = "true";

static const char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] =
    "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
static const char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
static const char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
static const char AWS_EC2_METADATA_DISABLED[] = "AWS_EC2_METADATA_DISABLED";

static const std::chrono::hours REFRESH_INTERVAL{1};
static const std::chrono::seconds REFRESH_GRACE_PERIOD{5};
static const char EC2_METADATA_HOST[] = "169.254.169.254:80";
static const char CONTAINER_METADATA_HOST[] = "169.254.170.2:80";
static const char SECURITY_CREDENTIALS_PATH[] = "/latest/meta-data/iam/security-credentials";

Credentials EnvironmentCredentialsProvider::getCredentials() {
  ENVOY_LOG(debug, "Getting AWS credentials from the environment");

  const auto access_key_id = std::getenv(AWS_ACCESS_KEY_ID);
  if (access_key_id == nullptr) {
    return Credentials();
  }

  const auto secret_access_key = std::getenv(AWS_SECRET_ACCESS_KEY);
  const auto session_token = std::getenv(AWS_SESSION_TOKEN);

  ENVOY_LOG(debug, "Found following AWS credentials in the environment: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id ? access_key_id : "", AWS_SECRET_ACCESS_KEY,
            secret_access_key ? "*****" : "", AWS_SESSION_TOKEN, session_token ? "*****" : "");

  return Credentials::fromCString(access_key_id, secret_access_key, session_token);
}

void MetadataCredentialsProviderBase::refreshIfNeeded() {
  const Thread::LockGuard lock(lock_);
  if (needsRefresh()) {
    refresh();
  }
}

bool InstanceProfileCredentialsProvider::needsRefresh() {
  return api_.timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

void InstanceProfileCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the instance metadata");

  // First discover the Role of this instance
  const auto instance_role_string =
      metadata_fetcher_(EC2_METADATA_HOST, SECURITY_CREDENTIALS_PATH, absl::nullopt);
  if (!instance_role_string) {
    ENVOY_LOG(error, "Could not retrieve credentials listing from the instance metadata");
    return;
  }

  const auto instance_role_list =
      StringUtil::splitToken(StringUtil::trim(instance_role_string.value()), "\n");
  if (instance_role_list.empty()) {
    ENVOY_LOG(error, "No AWS credentials were found in the instance metadata");
    return;
  }
  ENVOY_LOG(debug, "AWS credentials list:\n{}", instance_role_string.value());

  // Only one Role can be associated with an instance:
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html
  const auto credential_path =
      std::string(SECURITY_CREDENTIALS_PATH) + "/" +
      std::string(instance_role_list[0].data(), instance_role_list[0].size());
  ENVOY_LOG(debug, "AWS credentials path: {}", credential_path);

  // Then fetch and parse the credentials
  const auto credential_document =
      metadata_fetcher_(EC2_METADATA_HOST, credential_path, absl::nullopt);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the instance metadata");
    return;
  }

  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document: {}", e.what());
    return;
  }

  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  const auto session_token = document_json->getString(TOKEN, "");

  ENVOY_LOG(debug, "Found following AWS credentials in the instance metadata: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  cached_credentials_ = Credentials::fromString(access_key_id, secret_access_key, session_token);
  last_updated_ = api_.timeSource().systemTime();
}

bool TaskRoleCredentialsProvider::needsRefresh() {
  const auto now = api_.timeSource().systemTime();
  return (now - last_updated_ > REFRESH_INTERVAL) ||
         (expiration_time_ - now < REFRESH_GRACE_PERIOD);
}

void TaskRoleCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the task role at URI: {}", credential_uri_);

  absl::string_view host_view;
  absl::string_view path_view;
  Http::Utility::extractHostPathFromUri(credential_uri_, host_view, path_view);
  const auto credential_document =
      metadata_fetcher_(std::string(host_view.data(), host_view.size()),
                        std::string(path_view.data(), path_view.size()), authorization_token_);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the task role");
    return;
  }

  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
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
  cached_credentials_ = Credentials::fromString(access_key_id, secret_access_key, session_token);
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

  const auto relative_uri = std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI);
  const auto full_uri = std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI);
  const auto metadata_disabled = std::getenv(AWS_EC2_METADATA_DISABLED);

  if (relative_uri != nullptr) {
    const auto uri = std::string(CONTAINER_METADATA_HOST) + relative_uri;
    ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", uri);
    add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, uri,
                                                    absl::optional<std::string>()));
  } else if (full_uri != nullptr) {
    const auto authorization_token = std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN);
    if (authorization_token != nullptr) {
      ENVOY_LOG(debug,
                "Using task role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri,
                                                      authorization_token));
    } else {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri,
                                                      absl::optional<std::string>()));
    }
  } else if (metadata_disabled == nullptr || strncmp(metadata_disabled, TRUE, strlen(TRUE)) != 0) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(api, metadata_fetcher));
  }
}

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
