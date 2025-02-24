#include "source/extensions/common/aws/credentials_provider_impl.h"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/lock_guard.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "fmt/chrono.h"
#include "metadata_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
using std::chrono::seconds;

namespace {

constexpr char AWS_ACCESS_KEY_ID[] = "AWS_ACCESS_KEY_ID";
constexpr char AWS_SECRET_ACCESS_KEY[] = "AWS_SECRET_ACCESS_KEY";
constexpr char AWS_SESSION_TOKEN[] = "AWS_SESSION_TOKEN";
constexpr char AWS_ROLE_ARN[] = "AWS_ROLE_ARN";
constexpr char AWS_WEB_IDENTITY_TOKEN_FILE[] = "AWS_WEB_IDENTITY_TOKEN_FILE";
constexpr char AWS_ROLE_SESSION_NAME[] = "AWS_ROLE_SESSION_NAME";

constexpr char CREDENTIALS[] = "Credentials";
constexpr char ACCESS_KEY_ID[] = "AccessKeyId";
constexpr char SECRET_ACCESS_KEY[] = "SecretAccessKey";
constexpr char TOKEN[] = "Token";
constexpr char EXPIRATION[] = "Expiration";
constexpr char EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";
constexpr char TRUE[] = "true";
constexpr char SESSION_TOKEN[] = "SessionToken";
constexpr char WEB_IDENTITY_RESPONSE_ELEMENT[] = "AssumeRoleWithWebIdentityResponse";
constexpr char WEB_IDENTITY_RESULT_ELEMENT[] = "AssumeRoleWithWebIdentityResult";

constexpr char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] = "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
constexpr char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE";
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

constexpr char EC2_METADATA_CLUSTER[] = "ec2_instance_metadata_server_internal";
constexpr char CONTAINER_METADATA_CLUSTER[] = "ecs_task_metadata_server_internal";
constexpr char STS_TOKEN_CLUSTER[] = "sts_token_service_internal";

} // namespace

Credentials ConfigCredentialsProvider::getCredentials() {
  ENVOY_LOG(debug, "Getting AWS credentials from static configuration");
  return credentials_;
}

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
  if (needsRefresh()) {
    refresh();
  }
}

// TODO(suniltheta): The field context is of type ServerFactoryContextOptRef so
// that an optional empty value can be set. Especially in aws iam plugin the cluster manager
// obtained from server factory context object is not fully initialized due to the
// reasons explained in https://github.com/envoyproxy/envoy/issues/27586 which cannot
// utilize http async client here to fetch AWS credentials. For time being if context
// is empty then will use libcurl to fetch the credentials.

MetadataCredentialsProviderBase::MetadataCredentialsProviderBase(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    absl::string_view cluster_name, const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer)
    : api_(api), context_(context), fetch_metadata_using_curl_(fetch_metadata_using_curl),
      create_metadata_fetcher_cb_(create_metadata_fetcher_cb), cluster_name_(cluster_name),
      cache_duration_(getCacheDuration()), refresh_state_(refresh_state),
      initialization_timer_(initialization_timer), aws_cluster_manager_(aws_cluster_manager) {

  // Most code sets the context and uses the async http client, except for one extension
  // which is scheduled to be deprecated and deleted. Modes can no longer be switched via runtime,
  // so each caller should only pass parameters to support a single mode.
  // https://github.com/envoyproxy/envoy/issues/36910
  ASSERT((context.has_value() ^ (fetch_metadata_using_curl != nullptr)));

  // Async provider cluster setup
  if (context_) {
    // Set up metadata credentials statistics
    scope_ = api.rootScope().createScope(
        fmt::format("aws.metadata_credentials_provider.{}.", cluster_name_));
    stats_ = std::make_shared<MetadataCredentialsProviderStats>(MetadataCredentialsProviderStats{
        ALL_METADATACREDENTIALSPROVIDER_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))});
    stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));

    tls_slot_ =
        ThreadLocal::TypedSlot<ThreadLocalCredentialsCache>::makeUnique(context_->threadLocal());

    tls_slot_->set(
        [&](Event::Dispatcher&) { return std::make_shared<ThreadLocalCredentialsCache>(); });
  }
};

void MetadataCredentialsProviderBase::onClusterAddOrUpdate() {
  ENVOY_LOG(debug, "Received callback from aws cluster manager for cluster {}", cluster_name_);
  if (!cache_duration_timer_) {
    cache_duration_timer_ = context_->mainThreadDispatcher().createTimer([this]() -> void {
      stats_->credential_refreshes_performed_.inc();
      refresh();
    });
  }
  if (!cache_duration_timer_->enabled()) {
    cache_duration_timer_->enableTimer(std::chrono::milliseconds(1));
  }
}

void MetadataCredentialsProviderBase::credentialsRetrievalError() {
  // Credential retrieval failed, so set blank (anonymous) credentials
  if (context_) {
    stats_->credential_refreshes_failed_.inc();
    ENVOY_LOG(debug, "Error retrieving credentials, settings anonymous credentials");
    setCredentialsToAllThreads(std::make_unique<Credentials>());
    handleFetchDone();
  }
}

// Async provider uses its own refresh mechanism. Calling refreshIfNeeded() here is not thread safe.
bool MetadataCredentialsProviderBase::credentialsPending() {
  if (context_) {
    return credentials_pending_;
  }
  return false;
}

// Async provider uses its own refresh mechanism. Calling refreshIfNeeded() here is not thread safe.
Credentials MetadataCredentialsProviderBase::getCredentials() {

  if (context_) {
    if (tls_slot_) {
      return *(*tls_slot_)->credentials_.get();
    } else {
      return Credentials();
    }

  } else {
    // Refresh for non async case
    refreshIfNeeded();
    return cached_credentials_;
  }
}

std::chrono::seconds MetadataCredentialsProviderBase::getCacheDuration() {
  return std::chrono::seconds(
      REFRESH_INTERVAL -
      REFRESH_GRACE_PERIOD /*TODO: Add jitter from context.api().randomGenerator()*/);
}

void MetadataCredentialsProviderBase::handleFetchDone() {
  if (context_) {
    if (cache_duration_timer_ && !cache_duration_timer_->enabled()) {
      // Receiver state handles the initial credential refresh scenario. If for some reason we are
      // unable to perform credential refresh after cluster initialization has completed, we use a
      // short timer to keep retrying. Once successful, we fall back to the normal cache duration
      // or whatever expiration is provided in the credential payload
      if (refresh_state_ == MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh) {
        cache_duration_timer_->enableTimer(initialization_timer_);
        ENVOY_LOG(debug, "Metadata fetcher initialization failed, retrying in {}",
                  std::chrono::seconds(initialization_timer_.count()));
        // Timer begins at 2 seconds and doubles each time, to a maximum of 32 seconds. This avoids
        // excessive retries against STS or instance metadata service
        if (initialization_timer_ < std::chrono::seconds(32)) {
          initialization_timer_ = initialization_timer_ * 2;
        }
      } else {
        // If our returned token had an expiration time, use that to set the cache duration
        const auto now = api_.timeSource().systemTime();
        if (expiration_time_.has_value() && (expiration_time_.value() > now)) {
          cache_duration_ =
              std::chrono::duration_cast<std::chrono::seconds>(expiration_time_.value() - now);
          ENVOY_LOG(debug,
                    "Metadata fetcher setting credential refresh to {}, based on "
                    "credential expiration",
                    std::chrono::seconds(cache_duration_.count()));
        } else {
          cache_duration_ = getCacheDuration();
          ENVOY_LOG(
              debug,
              "Metadata fetcher setting credential refresh to {}, based on default expiration",
              std::chrono::seconds(cache_duration_.count()));
        }
        cache_duration_timer_->enableTimer(cache_duration_);
      }
    }
  }
}

void MetadataCredentialsProviderBase::setCredentialsToAllThreads(
    CredentialsConstUniquePtr&& creds) {

  ENVOY_LOG(debug, "{}: Setting credentials to all threads", this->providerName());

  CredentialsConstSharedPtr shared_credentials = std::move(creds);
  if (tls_slot_ && !tls_slot_->isShutdown()) {
    tls_slot_->runOnAllThreads(
        /* Set the credentials */ [shared_credentials](
                                      OptRef<ThreadLocalCredentialsCache>
                                          obj) { obj->credentials_ = shared_credentials; },
        /* Notify waiting signers on completion of credential setting above */
        [this]() {
          credentials_pending_.store(false);
          std::list<CredentialSubscriberCallbacks*> subscribers_copy;
          {
            Thread::LockGuard guard(mu_);
            subscribers_copy = credentials_subscribers_;
          }
          for (auto& cb : subscribers_copy) {
            ENVOY_LOG(debug, "Notifying subscriber of credential update");
            cb->onCredentialUpdate();
          }
        });
  }
}

CredentialSubscriberCallbacksHandlePtr
MetadataCredentialsProviderBase::subscribeToCredentialUpdates(CredentialSubscriberCallbacks& cs) {
  Thread::LockGuard guard(mu_);
  return std::make_unique<CredentialSubscriberCallbacksHandle>(cs, credentials_subscribers_);
}

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
      ENVOY_LOG_MISC(info, "Invalid credential file data source");
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

InstanceProfileCredentialsProvider::InstanceProfileCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view cluster_name)
    : MetadataCredentialsProviderBase(api, context, aws_cluster_manager, cluster_name,
                                      fetch_metadata_using_curl, create_metadata_fetcher_cb,
                                      refresh_state, initialization_timer) {}

bool InstanceProfileCredentialsProvider::needsRefresh() {
  return api_.timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

void InstanceProfileCredentialsProvider::refresh() {

  ENVOY_LOG(debug, "Getting AWS credentials from the EC2MetadataService");

  // First request for a session TOKEN so that we can call EC2MetadataService securely.
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configuring-instance-metadata-service.html
  Http::RequestMessageImpl token_req_message;
  token_req_message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  token_req_message.headers().setMethod(Http::Headers::get().MethodValues.Put);
  token_req_message.headers().setHost(EC2_METADATA_HOST);
  token_req_message.headers().setPath(EC2_IMDS_TOKEN_RESOURCE);
  token_req_message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_TTL_HEADER),
                                      EC2_IMDS_TOKEN_TTL_DEFAULT_VALUE);

  if (!context_) {
    // Using curl to fetch the AWS credentials where we first get the token.
    const auto token_string = fetch_metadata_using_curl_(token_req_message);
    if (token_string) {
      ENVOY_LOG(debug, "Obtained IMDSv2 token to make secure call to EC2MetadataService");
      fetchInstanceRole(std::move(token_string.value()));
    } else {
      ENVOY_LOG(warn, "Failed to get IMDSv2 token from EC2MetadataService, falling back to IMDSv1");
      fetchInstanceRole(std::move(""));
    }
  } else {
    // Stop any existing timer.
    if (cache_duration_timer_ && cache_duration_timer_->enabled()) {
      cache_duration_timer_->disableTimer();
    }
    // Using Http async client to fetch the AWS credentials where we first get the token.
    if (!metadata_fetcher_) {
      metadata_fetcher_ = create_metadata_fetcher_cb_(context_->clusterManager(), clusterName());
    } else {
      metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
    }
    on_async_fetch_cb_ = [this](const std::string&& arg) {
      return this->fetchInstanceRoleAsync(std::move(arg));
    };
    continue_on_async_fetch_failure_ = true;
    continue_on_async_fetch_failure_reason_ = "Token fetch failed, falling back to IMDSv1";

    // mark credentials as pending while async completes
    credentials_pending_.store(true);

    metadata_fetcher_->fetch(token_req_message, Tracing::NullSpan::instance(), *this);
  }
}

void InstanceProfileCredentialsProvider::fetchInstanceRole(const std::string&& token_string,
                                                           bool async /*default = false*/) {
  // Discover the Role of this instance.
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(EC2_METADATA_HOST);
  message.headers().setPath(SECURITY_CREDENTIALS_PATH);
  if (!token_string.empty()) {
    message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_HEADER),
                              StringUtil::trim(token_string));
  }

  if (!async) {
    // Using curl to fetch the Instance Role.
    const auto instance_role_string = fetch_metadata_using_curl_(message);
    if (!instance_role_string) {
      ENVOY_LOG(error, "Could not retrieve credentials listing from the EC2MetadataService");
      return;
    }
    fetchCredentialFromInstanceRole(std::move(instance_role_string.value()),
                                    std::move(token_string));
  } else {
    // Using Http async client to fetch the Instance Role.
    metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
    on_async_fetch_cb_ = [this, token_string = std::move(token_string)](const std::string&& arg) {
      return this->fetchCredentialFromInstanceRoleAsync(std::move(arg), std::move(token_string));
    };

    // mark credentials as pending while async completes
    credentials_pending_.store(true);

    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void InstanceProfileCredentialsProvider::fetchCredentialFromInstanceRole(
    const std::string&& instance_role, const std::string&& token_string,
    bool async /*default = false*/) {

  if (instance_role.empty()) {
    ENVOY_LOG(error, "No roles found to fetch AWS credentials from the EC2MetadataService");
    if (async) {
      credentialsRetrievalError();
    }
    return;
  }
  const auto instance_role_list = StringUtil::splitToken(StringUtil::trim(instance_role), "\n");
  if (instance_role_list.empty()) {
    ENVOY_LOG(error, "No roles found to fetch AWS credentials from the EC2MetadataService");
    if (async) {
      credentialsRetrievalError();
    }
    return;
  }
  ENVOY_LOG(debug, "AWS credentials list:\n{}", instance_role);

  // Only one Role can be associated with an instance:
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html
  const auto credential_path =
      std::string(SECURITY_CREDENTIALS_PATH) + "/" +
      std::string(instance_role_list[0].data(), instance_role_list[0].size());
  ENVOY_LOG(debug, "AWS credentials path: {}", credential_path);

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(EC2_METADATA_HOST);
  message.headers().setPath(credential_path);
  if (!token_string.empty()) {
    message.headers().setCopy(Http::LowerCaseString(EC2_IMDS_TOKEN_HEADER),
                              StringUtil::trim(token_string));
  }

  if (!async) {
    // Fetch and parse the credentials.
    const auto credential_document = fetch_metadata_using_curl_(message);
    if (!credential_document) {
      ENVOY_LOG(error, "Could not load AWS credentials document from the EC2MetadataService");
      return;
    }
    extractCredentials(std::move(credential_document.value()));
  } else {
    // Using Http async client to fetch and parse the AWS credentials.
    metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
    on_async_fetch_cb_ = [this](const std::string&& arg) {
      return this->extractCredentialsAsync(std::move(arg));
    };

    // mark credentials as pending while async completes
    credentials_pending_.store(true);

    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void InstanceProfileCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value, bool async /*default = false*/) {
  if (credential_document_value.empty()) {
    if (async) {
      ENVOY_LOG(error, "Empty AWS credentials document");
      credentialsRetrievalError();
    }
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> document_json_or_error;
  document_json_or_error = Json::Factory::loadFromString(credential_document_value);
  if (!document_json_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document: {}",
              document_json_or_error.status().message());
    if (async) {
      credentialsRetrievalError();
    }
    return;
  }

  const auto access_key_id =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), ACCESS_KEY_ID, "");
  const auto secret_access_key =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), SECRET_ACCESS_KEY, "");
  const auto session_token =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), TOKEN, "");

  ENVOY_LOG(debug,
            "Obtained following AWS credentials from the EC2MetadataService: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  last_updated_ = api_.timeSource().systemTime();
  if (context_) {
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
    stats_->credential_refreshes_succeeded_.inc();
    ENVOY_LOG(debug, "Metadata receiver moving to Ready state");
    refresh_state_ = MetadataFetcher::MetadataReceiver::RefreshState::Ready;
    // Set receiver state in statistics
    stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));
  } else {
    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  handleFetchDone();
}

void InstanceProfileCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS Instance metadata fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void InstanceProfileCredentialsProvider::onMetadataError(Failure reason) {
  // Credential retrieval failed, so set blank (anonymous) credentials
  credentialsRetrievalError();
  if (continue_on_async_fetch_failure_) {
    ENVOY_LOG(warn, "{}. Reason: {}", continue_on_async_fetch_failure_reason_,
              metadata_fetcher_->failureToString(reason));
    continue_on_async_fetch_failure_ = false;
    continue_on_async_fetch_failure_reason_ = "";
    on_async_fetch_cb_(std::move(""));
  } else {
    ENVOY_LOG(error, "AWS Instance metadata fetch failure: {}",
              metadata_fetcher_->failureToString(reason));
    handleFetchDone();
  }
}

ContainerCredentialsProvider::ContainerCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view credential_uri,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view authorization_token,
    absl::string_view cluster_name)
    : MetadataCredentialsProviderBase(api, context, aws_cluster_manager, cluster_name,
                                      fetch_metadata_using_curl, create_metadata_fetcher_cb,
                                      refresh_state, initialization_timer),
      credential_uri_(credential_uri), authorization_token_(authorization_token) {}

bool ContainerCredentialsProvider::needsRefresh() {
  const auto now = api_.timeSource().systemTime();
  auto expired = (now - last_updated_ > REFRESH_INTERVAL);

  if (expiration_time_.has_value()) {
    return expired || (expiration_time_.value() - now < REFRESH_GRACE_PERIOD);
  } else {
    return expired;
  }
}

void ContainerCredentialsProvider::refresh() {

  absl::string_view host, path;

  if (!context_) {
    ENVOY_LOG(debug, "Getting AWS credentials from the container role at URI: {}", credential_uri_);
    Http::Utility::extractHostPathFromUri(credential_uri_, host, path);
  } else {
    ENVOY_LOG(debug, "Getting AWS credentials from the container role at URI: {}",
              aws_cluster_manager_.ref()->getUriFromClusterName(cluster_name_).value());
    Http::Utility::extractHostPathFromUri(
        aws_cluster_manager_.ref()->getUriFromClusterName(cluster_name_).value(), host, path);
  }

  // ECS Task role: use const authorization_token set during initialization
  absl::string_view authorization_header = authorization_token_;
  absl::StatusOr<std::string> token_or_error;

  if (authorization_token_.empty()) {
    // EKS Pod Identity token is sourced from AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE
    if (const auto token_file = std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE)) {
      token_or_error = api_.fileSystem().fileReadToEnd(std::string(token_file));
      if (token_or_error.ok()) {
        ENVOY_LOG_MISC(debug, "Container authorization token file contents loaded");
        authorization_header = token_or_error.value();
      }
    }
  }

  Http::Utility::extractHostPathFromUri(credential_uri_, host, path);

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(host);
  message.headers().setPath(path);
  message.headers().setCopy(Http::CustomHeaders::get().Authorization, authorization_header);
  if (!context_) {
    // Using curl to fetch the AWS credentials.
    const auto credential_document = fetch_metadata_using_curl_(message);
    if (!credential_document) {
      ENVOY_LOG(error, "Could not load AWS credentials document from the container role");
      return;
    }
    extractCredentials(std::move(credential_document.value()));
  } else {
    // Stop any existing timer.
    if (cache_duration_timer_ && cache_duration_timer_->enabled()) {
      cache_duration_timer_->disableTimer();
    }
    // Using Http async client to fetch the AWS credentials.
    if (!metadata_fetcher_) {
      metadata_fetcher_ = create_metadata_fetcher_cb_(context_->clusterManager(), clusterName());
    } else {
      metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
    }
    on_async_fetch_cb_ = [this](const std::string&& arg) {
      return this->extractCredentials(std::move(arg));
    };

    // mark credentials as pending while async completes
    credentials_pending_.store(true);

    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void ContainerCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  if (credential_document_value.empty()) {
    credentialsRetrievalError();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> document_json_or_error;

  document_json_or_error = Json::Factory::loadFromString(credential_document_value);
  if (!document_json_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from the container role: {}",
              document_json_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  const auto access_key_id =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), ACCESS_KEY_ID, "");
  const auto secret_access_key =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), SECRET_ACCESS_KEY, "");
  const auto session_token =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), TOKEN, "");

  ENVOY_LOG(debug, "Found following AWS credentials in the container role: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  const auto expiration_str =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), EXPIRATION, "");

  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "Container role AWS credentials expiration time: {}", expiration_str);
      expiration_time_ = absl::ToChronoTime(expiration_time);
    }
  }

  last_updated_ = api_.timeSource().systemTime();
  if (context_) {
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
    stats_->credential_refreshes_succeeded_.inc();

    ENVOY_LOG(debug, "Metadata receiver {} moving to Ready state", cluster_name_);
    refresh_state_ = MetadataFetcher::MetadataReceiver::RefreshState::Ready;
    // Set receiver state in statistics
    stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));
  } else {
    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  handleFetchDone();
}

void ContainerCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS Task metadata fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void ContainerCredentialsProvider::onMetadataError(Failure reason) {
  // Credential retrieval failed, so set blank (anonymous) credentials
  ENVOY_LOG(error, "AWS metadata fetch failure: {}", metadata_fetcher_->failureToString(reason));
  credentialsRetrievalError();
}

WebIdentityCredentialsProvider::WebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerOptRef aws_cluster_manager, absl::string_view cluster_name,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config)
    : MetadataCredentialsProviderBase(context.api(), context, aws_cluster_manager, cluster_name,
                                      nullptr, create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
      role_arn_(web_identity_config.role_arn()),
      role_session_name_(web_identity_config.role_session_name()) {

  auto provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      web_identity_config.web_identity_token_data_source(), context.mainThreadDispatcher(),
      context.threadLocal(), context.api(), false, 4096);
  if (provider_or_error_.ok()) {
    web_identity_data_source_provider_ = std::move(provider_or_error_.value());
  } else {
    ENVOY_LOG_MISC(info, "Invalid web identity data source");
    web_identity_data_source_provider_.reset();
  }
}

bool WebIdentityCredentialsProvider::needsRefresh() {

  const auto now = api_.timeSource().systemTime();
  auto expired = (now - last_updated_ > REFRESH_INTERVAL);

  if (expiration_time_.has_value()) {
    return expired || (expiration_time_.value() - now < REFRESH_GRACE_PERIOD);
  } else {
    return expired;
  }
}

void WebIdentityCredentialsProvider::refresh() {

  absl::string_view web_identity_data;

  // If we're unable to read from the configured data source, exit early.
  if (!web_identity_data_source_provider_.has_value()) {
    return;
  }

  ENVOY_LOG(debug, "Getting AWS web identity credentials from STS: {}",
            aws_cluster_manager_.ref()->getUriFromClusterName(cluster_name_).value());
  web_identity_data = web_identity_data_source_provider_.value()->data();

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  auto statusOr = aws_cluster_manager_.ref()->getUriFromClusterName(cluster_name_);
  message.headers().setHost(Http::Utility::parseAuthority(statusOr.value()).host_);
  message.headers().setPath(
      fmt::format("/?Action=AssumeRoleWithWebIdentity"
                  "&Version=2011-06-15"
                  "&RoleSessionName={}"
                  "&RoleArn={}"
                  "&WebIdentityToken={}",
                  Envoy::Http::Utility::PercentEncoding::encode(role_session_name_),
                  Envoy::Http::Utility::PercentEncoding::encode(role_arn_),
                  Envoy::Http::Utility::PercentEncoding::encode(web_identity_data)));
  // Use the Accept header to ensure that AssumeRoleWithWebIdentityResponse is returned as JSON.
  message.headers().setReference(Http::CustomHeaders::get().Accept,
                                 Http::Headers::get().ContentTypeValues.Json);
  // Stop any existing timer.
  if (cache_duration_timer_ && cache_duration_timer_->enabled()) {
    cache_duration_timer_->disableTimer();
  }
  // Using Http async client to fetch the AWS credentials.
  if (!metadata_fetcher_) {
    metadata_fetcher_ = create_metadata_fetcher_cb_(context_->clusterManager(), clusterName());
  } else {
    metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
  }

  on_async_fetch_cb_ = [this](const std::string&& arg) {
    return this->extractCredentials(std::move(arg));
  };

  // mark credentials as pending while async completes
  credentials_pending_.store(true);

  metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
}

void WebIdentityCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  if (credential_document_value.empty()) {
    ENVOY_LOG(error, "Could not load AWS credentials document from STS");
    credentialsRetrievalError();
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> document_json_or_error;
  document_json_or_error = Json::Factory::loadFromString(credential_document_value);
  if (!document_json_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from STS: {}",
              document_json_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> root_node =
      document_json_or_error.value()->getObject(WEB_IDENTITY_RESPONSE_ELEMENT);
  if (!root_node.ok()) {
    ENVOY_LOG(error, "AWS STS credentials document is empty");
    credentialsRetrievalError();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> result_node =
      root_node.value()->getObject(WEB_IDENTITY_RESULT_ELEMENT);
  if (!result_node.ok()) {
    ENVOY_LOG(error, "AWS STS returned an unexpected result");
    credentialsRetrievalError();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> credentials = result_node.value()->getObject(CREDENTIALS);
  if (!credentials.ok()) {
    ENVOY_LOG(error, "AWS STS credentials document does not contain any credentials");
    credentialsRetrievalError();
    return;
  }

  const auto access_key_id =
      Utility::getStringFromJsonOrDefault(credentials.value(), ACCESS_KEY_ID, "");
  const auto secret_access_key =
      Utility::getStringFromJsonOrDefault(credentials.value(), SECRET_ACCESS_KEY, "");
  const auto session_token =
      Utility::getStringFromJsonOrDefault(credentials.value(), SESSION_TOKEN, "");

  // Mandatory response fields
  if (access_key_id.empty() || secret_access_key.empty() || session_token.empty()) {
    ENVOY_LOG(error, "Bad format, could not parse AWS credentials document from STS");
    credentialsRetrievalError();
    return;
  }

  ENVOY_LOG(debug, "Received the following AWS credentials from STS: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");
  setCredentialsToAllThreads(
      std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
  stats_->credential_refreshes_succeeded_.inc();

  ENVOY_LOG(debug, "Metadata receiver {} moving to Ready state", cluster_name_);
  refresh_state_ = MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  // Set receiver state in statistics
  stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));

  const auto expiration = Utility::getIntegerFromJsonOrDefault(credentials.value(), EXPIRATION, 0);

  if (expiration != 0) {
    expiration_time_ =
        std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(expiration));
    ENVOY_LOG(debug, "AWS STS credentials expiration time (unix timestamp): {}", expiration);
  } else {
    // We don't have a valid expiration time from the json response
    expiration_time_.reset();
  }

  last_updated_ = api_.timeSource().systemTime();
  handleFetchDone();
}

void WebIdentityCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS metadata fetch from STS success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void WebIdentityCredentialsProvider::onMetadataError(Failure reason) {
  ENVOY_LOG(error, "AWS metadata fetch failure: {}", metadata_fetcher_->failureToString(reason));
  credentialsRetrievalError();
}

// Determine if we have a provider that is pending, based on priority ordering in the chain.
// Ignore any non-pending providers that have no credentials for us.

bool CredentialsProviderChain::chainProvidersPending() {
  for (auto& provider : providers_) {
    if (provider->credentialsPending()) {
      ENVOY_LOG(debug, "Provider {} is still pending", provider->providerName());
      return true;
    }
    if (provider->getCredentials().hasCredentials()) {
      ENVOY_LOG(debug, "Provider {} has credentials", provider->providerName());
      break;
    } else {
      ENVOY_LOG(debug, "Provider {} has blank credentials, continuing through chain",
                provider->providerName());
    }
  }
  return false;
}

bool CredentialsProviderChain::addCallbackIfChainCredentialsPending(
    CredentialsPendingCallback&& cb) {
  if (!chainProvidersPending()) {
    return false;
  }
  if (cb) {
    ENVOY_LOG(debug, "Adding credentials pending callback to queue");
    Thread::LockGuard guard(mu_);
    credential_pending_callbacks_.push_back(std::move(cb));
    ENVOY_LOG(debug, "We have {} pending callbacks", credential_pending_callbacks_.size());
  }
  return true;
}

void CredentialsProviderChain::onCredentialUpdate() {
  if (chainProvidersPending()) {
    return;
  }

  std::vector<CredentialsPendingCallback> callbacks_copy;

  {
    Thread::LockGuard guard(mu_);
    callbacks_copy = credential_pending_callbacks_;
    credential_pending_callbacks_.clear();
  }

  ENVOY_LOG(debug, "Notifying {} credential callbacks", callbacks_copy.size());

  // Call all of our callbacks to unblock pending requests
  for (const auto& cb : callbacks_copy) {
    cb();
  }
}

Credentials CredentialsProviderChain::chainGetCredentials() {
  for (auto& provider : providers_) {
    const auto credentials = provider->getCredentials();
    if (credentials.hasCredentials()) {
      return credentials;
    }
  }

  ENVOY_LOG(debug, "No AWS credentials found, using anonymous credentials");
  return Credentials();
}

std::string sessionName(Api::Api& api) {
  const auto role_session_name = absl::NullSafeStringView(std::getenv(AWS_ROLE_SESSION_NAME));
  std::string actual_session_name;
  if (!role_session_name.empty()) {
    actual_session_name = std::string(role_session_name);
  } else {
    // In practice, this value will be provided by the environment, so the placeholder value is
    // not important. Some AWS SDKs use time in nanoseconds, so we'll just use that.
    const auto now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               api.timeSource().systemTime().time_since_epoch())
                               .count();
    actual_session_name = fmt::format("{}", now_nanos);
  }
  return actual_session_name;
}

std::string stsClusterName(absl::string_view region) {
  return absl::StrCat(STS_TOKEN_CLUSTER, "-", region);
}

SINGLETON_MANAGER_REGISTRATION(aws_cluster_manager);

CustomCredentialsProviderChain::CustomCredentialsProviderChain(
    Server::Configuration::ServerFactoryContext& context, absl::string_view region,
    const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
    CustomCredentialsProviderChainFactories& factories) {

  aws_cluster_manager_ =
      context.singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
          [&context] {
            return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(context);
          },
          true);

  // Custom chain currently only supports file based and web identity credentials
  if (credential_provider_config.has_assume_role_with_web_identity_provider()) {
    auto web_identity = credential_provider_config.assume_role_with_web_identity_provider();
    std::string role_session_name = web_identity.role_session_name();
    if (role_session_name.empty()) {
      web_identity.set_role_session_name(sessionName(context.api()));
    }
    add(factories.createWebIdentityCredentialsProvider(context, aws_cluster_manager_, region,
                                                       web_identity));
  }

  if (credential_provider_config.has_credentials_file_provider()) {
    add(factories.createCredentialsFileCredentialsProvider(
        context, credential_provider_config.credentials_file_provider()));
  }
}

DefaultCredentialsProviderChain::DefaultCredentialsProviderChain(
    Api::Api& api, ServerFactoryContextOptRef context, absl::string_view region,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
    CredentialsProviderChainFactories& factories) {

  if (context) {
    aws_cluster_manager_ =
        context->singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
            SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
            [&context] {
              return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
                  context.value());
            },
            true);
  }

  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());

  // Initial state for an async credential receiver
  auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  // Initial amount of time for async credential receivers to wait for an initial refresh to succeed
  auto initialization_timer = std::chrono::seconds(2);

  if (context) {

    ENVOY_LOG(debug, "Using credentials file credentials provider");
    add(factories.createCredentialsFileCredentialsProvider(
        context.value(), credential_provider_config.credentials_file_provider()));

    auto web_identity = credential_provider_config.assume_role_with_web_identity_provider();

    // Configure defaults if nothing is set in the config
    if (!web_identity.has_web_identity_token_data_source()) {
      web_identity.mutable_web_identity_token_data_source()->set_filename(
          absl::NullSafeStringView(std::getenv(AWS_WEB_IDENTITY_TOKEN_FILE)));
    }

    if (web_identity.role_arn().empty()) {
      web_identity.set_role_arn(absl::NullSafeStringView(std::getenv(AWS_ROLE_ARN)));
    }

    if (web_identity.role_session_name().empty()) {
      web_identity.set_role_session_name(sessionName(api));
    }

    if ((!web_identity.web_identity_token_data_source().filename().empty() ||
         !web_identity.web_identity_token_data_source().inline_bytes().empty() ||
         !web_identity.web_identity_token_data_source().inline_string().empty() ||
         !web_identity.web_identity_token_data_source().environment_variable().empty()) &&
        !web_identity.role_arn().empty()) {

      const auto sts_endpoint = Utility::getSTSEndpoint(region) + ":443";
      const auto cluster_name = stsClusterName(region);

      ENVOY_LOG(
          debug,
          "Using web identity credentials provider with STS endpoint: {} and session name: {}",
          sts_endpoint, web_identity.role_session_name());
      add(factories.createWebIdentityCredentialsProvider(context.value(), aws_cluster_manager_,
                                                         region, web_identity));
    }
  }

  // Even if WebIdentity is supported keep the fallback option open so that
  // Envoy can use other credentials provider if available.
  const auto relative_uri =
      absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI));
  const auto full_uri = absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI));
  const auto metadata_disabled = absl::NullSafeStringView(std::getenv(AWS_EC2_METADATA_DISABLED));

  if (!relative_uri.empty()) {
    const auto uri = absl::StrCat(CONTAINER_METADATA_HOST, relative_uri);
    ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", uri);
    add(factories.createContainerCredentialsProvider(
        api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
        MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, uri, refresh_state,
        initialization_timer));
  } else if (!full_uri.empty()) {
    auto authorization_token =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
    if (!authorization_token.empty()) {
      ENVOY_LOG(debug,
                "Using container role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createContainerCredentialsProvider(
          api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
          MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, full_uri, refresh_state,
          initialization_timer, authorization_token));
    } else {
      ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", full_uri);
      add(factories.createContainerCredentialsProvider(
          api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
          MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, full_uri, refresh_state,
          initialization_timer));
    }
  } else if (metadata_disabled != TRUE) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(
        api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
        MetadataFetcher::create, refresh_state, initialization_timer, EC2_METADATA_CLUSTER));
  }
}

SINGLETON_MANAGER_REGISTRATION(container_credentials_provider);
SINGLETON_MANAGER_REGISTRATION(instance_profile_credentials_provider);

CredentialsProviderSharedPtr DefaultCredentialsProviderChain::createContainerCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
    absl::string_view credential_uri, MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view authorization_token = {}) {

  // TODO: @nbaws Remove curl path post deprecation
  if (!context) {
    return std::make_shared<ContainerCredentialsProvider>(
        api, context, absl::nullopt, fetch_metadata_using_curl, create_metadata_fetcher_cb,
        credential_uri, refresh_state, initialization_timer, authorization_token, cluster_name);
  } else {

    auto status = aws_cluster_manager.ref()->addManagedCluster(
        cluster_name, envoy::config::cluster::v3::Cluster::STATIC, credential_uri);

    auto credential_provider =
        context->singletonManager()
            .getTyped<Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
                SINGLETON_MANAGER_REGISTERED_NAME(container_credentials_provider),
                [&context, &api, &aws_cluster_manager, fetch_metadata_using_curl,
                 create_metadata_fetcher_cb, &credential_uri, &refresh_state, &initialization_timer,
                 &authorization_token, &cluster_name] {
                  return std::make_shared<
                      Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
                      api, context, aws_cluster_manager, fetch_metadata_using_curl,
                      create_metadata_fetcher_cb, credential_uri, refresh_state,
                      initialization_timer, authorization_token, cluster_name);
                });

    auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
        cluster_name,
        *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
    if (handleOr.ok()) {
      credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
    }

    storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

    return credential_provider;
  }
}

CredentialsProviderSharedPtr
DefaultCredentialsProviderChain::createInstanceProfileCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view cluster_name) {

  if (!context) {
    return std::make_shared<InstanceProfileCredentialsProvider>(
        api, context, absl::nullopt, fetch_metadata_using_curl, create_metadata_fetcher_cb,
        refresh_state, initialization_timer, cluster_name);
  } else {

    auto status = aws_cluster_manager.ref()->addManagedCluster(
        cluster_name, envoy::config::cluster::v3::Cluster::STATIC, EC2_METADATA_HOST);
    auto credential_provider =
        context->singletonManager()
            .getTyped<Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
                SINGLETON_MANAGER_REGISTERED_NAME(instance_profile_credentials_provider),
                [&context, &api, &aws_cluster_manager, fetch_metadata_using_curl,
                 create_metadata_fetcher_cb, &refresh_state, &initialization_timer, &cluster_name] {
                  return std::make_shared<
                      Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
                      api, context, aws_cluster_manager, fetch_metadata_using_curl,
                      create_metadata_fetcher_cb, refresh_state, initialization_timer,
                      cluster_name);
                });

    auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
        cluster_name,
        *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
    if (handleOr.ok()) {

      credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
    }

    storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

    return credential_provider;
  }
}

CredentialsProviderSharedPtr DefaultCredentialsProviderChain::createWebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config) {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = stsClusterName(region);
  auto uri = Utility::getSTSEndpoint(region) + ":443";

  auto status = aws_cluster_manager.ref()->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto credential_provider = std::make_shared<WebIdentityCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, refresh_state,
      initialization_timer, web_identity_config);
  auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));

  if (handleOr.ok()) {

    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
};

void CredentialsProviderChain::storeSubscription(
    CredentialSubscriberCallbacksHandlePtr subscription) {
  subscriber_handles_.push_back(std::move(subscription));
}

CredentialsProviderSharedPtr CustomCredentialsProviderChain::createWebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config) {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = stsClusterName(region);
  auto uri = Utility::getSTSEndpoint(region) + ":443";

  auto status = aws_cluster_manager.ref()->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto credential_provider = std::make_shared<WebIdentityCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, refresh_state,
      initialization_timer, web_identity_config);
  auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));

  if (handleOr.ok()) {
    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }
  return credential_provider;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
