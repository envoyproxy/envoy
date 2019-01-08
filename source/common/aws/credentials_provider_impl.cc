#include "common/aws/credentials_provider_impl.h"

#include <ctime>

#include "envoy/common/exception.h"

#include "common/aws/metadata_fetcher_impl.h"
#include "common/common/lock_guard.h"
#include "common/common/utility.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Aws {
namespace Auth {

static const char AWS_ACCESS_KEY_ID[] = "AWS_ACCESS_KEY_ID";
static const char AWS_SECRET_ACCESS_KEY[] = "AWS_SECRET_ACCESS_KEY";
static const char AWS_SESSION_TOKEN[] = "AWS_SESSION_TOKEN";

static const char ACCESS_KEY_ID[] = "AccessKeyId";
static const char SECRET_ACCESS_KEY[] = "SecretAccessKey";
static const char TOKEN[] = "Token";
static const char EXPIRATION[] = "Expiration";
static const char EXPIRATION_FORMAT[] = "%Y%m%dT%H%M%S%z";
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
  const auto access_key_id = std::getenv(AWS_ACCESS_KEY_ID);
  Credentials credentials;
  if (access_key_id == nullptr) {
    return credentials;
  }
  ENVOY_LOG(debug, "Found environment credential {}={}", AWS_ACCESS_KEY_ID, access_key_id);
  credentials.setAccessKeyId(access_key_id);
  const auto secret_access_key = std::getenv(AWS_SECRET_ACCESS_KEY);
  if (secret_access_key != nullptr) {
    ENVOY_LOG(debug, "Found environment credential {}=*****", AWS_SECRET_ACCESS_KEY);
    credentials.setSecretAccessKey(secret_access_key);
  }
  const auto session_token = std::getenv(AWS_SESSION_TOKEN);
  if (session_token != nullptr) {
    ENVOY_LOG(debug, "Found environment credential {}=*****", AWS_SESSION_TOKEN);
    credentials.setSessionToken(session_token);
  }
  return credentials;
}

void MetadataCredentialsProviderBase::refreshIfNeeded() {
  Thread::LockGuard lock(lock_);
  if (!needsRefresh()) {
    return;
  }
  refresh();
}

bool InstanceProfileCredentialsProvider::needsRefresh() {
  if (time_system_.systemTime() - last_updated_ > REFRESH_INTERVAL) {
    return true;
  }
  return false;
}

void InstanceProfileCredentialsProvider::refresh() {
  auto dispatcher = api_.allocateDispatcher(time_system_);
  ENVOY_LOG(debug, "Getting default credentials for ec2 instance");
  // Get the list of credential names
  const auto credential_listing =
      fetcher_->getMetadata(*dispatcher, EC2_METADATA_HOST, SECURITY_CREDENTIALS_PATH);
  if (!credential_listing) {
    ENVOY_LOG(error, "Could not retrieve credentials listing");
    return;
  }
  const auto credential_names =
      StringUtil::splitToken(StringUtil::trim(credential_listing.value()), "\n");
  if (credential_names.empty()) {
    ENVOY_LOG(error, "No credentials were found");
    return;
  }
  ENVOY_LOG(debug, "Credentials found:\n{}", credential_listing.value());
  const auto credential_path = std::string(SECURITY_CREDENTIALS_PATH) + "/" +
                               std::string(credential_names[0].data(), credential_names[0].size());
  ENVOY_LOG(debug, "Loading credentials document from {}", credential_path);
  const auto credential_document =
      fetcher_->getMetadata(*dispatcher, EC2_METADATA_HOST, credential_path);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load credentials document");
    return;
  }
  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse credentials document: {}", e.what());
    return;
  }
  Credentials credentials;
  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  if (!access_key_id.empty()) {
    ENVOY_LOG(debug, "Found instance credential {}={}", ACCESS_KEY_ID, access_key_id);
    credentials.setAccessKeyId(access_key_id);
  }
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  if (!secret_access_key.empty()) {
    ENVOY_LOG(debug, "Found instance credential {}=*****", SECRET_ACCESS_KEY);
    credentials.setSecretAccessKey(secret_access_key);
  }
  const auto token = document_json->getString(TOKEN, "");
  if (!token.empty()) {
    ENVOY_LOG(debug, "Found instance credential {}=*****", TOKEN);
    credentials.setSessionToken(token);
  }
  cached_credentials_ = credentials;
  last_updated_ = time_system_.systemTime();
}

bool TaskRoleCredentialsProvider::needsRefresh() {
  if (time_system_.systemTime() - last_updated_ > REFRESH_INTERVAL) {
    return true;
  }
  if (expiration_time_ - time_system_.systemTime() < REFRESH_GRACE_PERIOD) {
    return true;
  }
  return false;
}

void TaskRoleCredentialsProvider::refresh() {
  auto dispatcher = api_.allocateDispatcher(time_system_);
  ENVOY_LOG(debug, "Getting ecs credentials");
  ENVOY_LOG(debug, "Loading credentials document from {}", credential_uri_);
  absl::string_view host_view;
  absl::string_view path_view;
  Http::Utility::extractHostPathFromUri(credential_uri_, host_view, path_view);
  const auto credential_document =
      fetcher_->getMetadata(*dispatcher, std::string(host_view.data(), host_view.size()),
                            std::string(path_view.data(), path_view.size()), authorization_token_);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load credentials document");
    return;
  }
  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse credentials document: {}", e.what());
    return;
  }
  Credentials credentials;
  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  if (!access_key_id.empty()) {
    ENVOY_LOG(debug, "Found task role credential {}={}", ACCESS_KEY_ID, access_key_id);
    credentials.setAccessKeyId(access_key_id);
  }
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  if (!secret_access_key.empty()) {
    ENVOY_LOG(debug, "Found task role credential {}=*****", SECRET_ACCESS_KEY);
    credentials.setSecretAccessKey(secret_access_key);
  }
  const auto token = document_json->getString(TOKEN, "");
  if (!token.empty()) {
    ENVOY_LOG(debug, "Found task role credential {}=*****", TOKEN);
    credentials.setSessionToken(token);
  }
  const auto expiration = document_json->getString(EXPIRATION, "");
  if (!expiration.empty()) {
    std::tm timestamp{};
    if (strptime(expiration.c_str(), EXPIRATION_FORMAT, &timestamp) ==
        (expiration.c_str() + expiration.size())) {
      ENVOY_LOG(debug, "Found task role credential {}={}", EXPIRATION, expiration);
      expiration_time_ = SystemTime::clock::from_time_t(std::mktime(&timestamp));
    }
  }
  cached_credentials_ = credentials;
  last_updated_ = time_system_.systemTime();
}

Credentials CredentialsProviderChain::getCredentials() {
  for (auto& provider : providers_) {
    const auto credentials = provider->getCredentials();
    if (credentials.accessKeyId() && credentials.secretAccessKey()) {
      return credentials;
    }
  }
  ENVOY_LOG(debug, "No credentials found. Using anonymous credentials");
  return Credentials();
}

DefaultCredentialsProviderChain::DefaultCredentialsProviderChain(
    Api::Api& api, Event::TimeSystem& time_system,
    const CredentialsProviderChainFactories& factories) {
  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());
  const auto relative_uri = std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI);
  const auto full_uri = std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI);
  const auto metadata_disabled = std::getenv(AWS_EC2_METADATA_DISABLED);
  if (relative_uri != nullptr) {
    const auto uri = std::string(CONTAINER_METADATA_HOST) + relative_uri;
    ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", uri);
    add(factories.createTaskRoleCredentialsProvider(
        api, time_system, factories.createMetadataFetcher(), uri, absl::optional<std::string>()));
  } else if (full_uri != nullptr) {
    const auto authorization_token = std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN);
    if (authorization_token != nullptr) {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {} and authorization token",
                full_uri);
      add(factories.createTaskRoleCredentialsProvider(
          api, time_system, factories.createMetadataFetcher(), full_uri, authorization_token));
    } else {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, time_system,
                                                      factories.createMetadataFetcher(), full_uri,
                                                      absl::optional<std::string>()));
    }
  } else if (metadata_disabled == nullptr || strncmp(metadata_disabled, TRUE, strlen(TRUE)) != 0) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(api, time_system,
                                                           factories.createMetadataFetcher()));
  }
}

MetadataFetcherPtr createMetadataFetcher(Api::Api& api, Event::TimeSystem& time_system);

MetadataFetcherPtr DefaultCredentialsProviderChain::createMetadataFetcher() const {
  return std::make_unique<MetadataFetcherImpl>();
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy