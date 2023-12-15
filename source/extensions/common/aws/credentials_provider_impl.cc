#include "source/extensions/common/aws/credentials_provider_impl.h"

#include <fstream>

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

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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

constexpr char EC2_METADATA_CLUSTER[] = "ec2_instance_metadata_server_internal";
constexpr char CONTAINER_METADATA_CLUSTER[] = "ecs_task_metadata_server_internal";
constexpr char STS_TOKEN_CLUSTER[] = "sts_token_service_internal";

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

// TODO(suniltheta): The field context is of type ServerFactoryContextOptRef so that an
// optional empty value can be set. Especially in aws iam plugin the cluster manager
// obtained from server factory context object is not fully initialized due to the
// reasons explained in https://github.com/envoyproxy/envoy/issues/27586 which cannot
// utilize http async client here to fetch AWS credentials. For time being if context
// is empty then will use libcurl to fetch the credentials.
MetadataCredentialsProviderBase::MetadataCredentialsProviderBase(
    Api::Api& api, ServerFactoryContextOptRef context,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type, absl::string_view uri)
    : api_(api), context_(context), fetch_metadata_using_curl_(fetch_metadata_using_curl),
      create_metadata_fetcher_cb_(create_metadata_fetcher_cb),
      cluster_name_(std::string(cluster_name)), cluster_type_(cluster_type), uri_(std::string(uri)),
      cache_duration_(getCacheDuration()),
      debug_name_(absl::StrCat("Fetching aws credentials from cluster=", cluster_name)) {
  if (context_) {
    context_->mainThreadDispatcher().post([this]() {
      if (!Utility::addInternalClusterStatic(context_->clusterManager(), cluster_name_,
                                             cluster_type_, uri_)) {
        ENVOY_LOG(critical,
                  "Failed to add [STATIC cluster = {} with address = {}] or cluster not found",
                  cluster_name_, uri_);
        return;
      }
    });

    tls_ = ThreadLocal::TypedSlot<ThreadLocalCredentialsCache>::makeUnique(context_->threadLocal());
    tls_->set(
        [](Envoy::Event::Dispatcher&) { return std::make_shared<ThreadLocalCredentialsCache>(); });

    cache_duration_timer_ = context_->mainThreadDispatcher().createTimer([this]() -> void {
      if (useHttpAsyncClient()) {
        const Thread::LockGuard lock(lock_);
        refresh();
      }
    });

    if (useHttpAsyncClient()) {
      // Register with init_manager, force the listener to wait for fetching (refresh).
      init_target_ =
          std::make_unique<Init::TargetImpl>(debug_name_, [this]() -> void { refresh(); });
      context_->initManager().add(*init_target_);
    }
  }
}

Credentials MetadataCredentialsProviderBase::getCredentials() {
  refreshIfNeeded();
  if (useHttpAsyncClient() && context_ && tls_) {
    // If server factor context was supplied then we would have thread local slot initialized.
    return *(*tls_)->credentials_.get();
  } else {
    return cached_credentials_;
  }
}

std::chrono::seconds MetadataCredentialsProviderBase::getCacheDuration() {
  return std::chrono::seconds(
      REFRESH_INTERVAL * 60 * 60 -
      REFRESH_GRACE_PERIOD /*TODO: Add jitter from context.api().randomGenerator()*/);
}

void MetadataCredentialsProviderBase::handleFetchDone() {
  if (useHttpAsyncClient() && context_) {
    if (init_target_) {
      init_target_->ready();
      init_target_.reset();
    }
    if (cache_duration_timer_ && !cache_duration_timer_->enabled()) {
      cache_duration_timer_->enableTimer(cache_duration_);
    }
  }
}

void MetadataCredentialsProviderBase::setCredentialsToAllThreads(
    CredentialsConstUniquePtr&& creds) {
  CredentialsConstSharedPtr shared_credentials = std::move(creds);
  if (tls_) {
    tls_->runOnAllThreads([shared_credentials](OptRef<ThreadLocalCredentialsCache> obj) {
      obj->credentials_ = shared_credentials;
    });
  }
}

bool MetadataCredentialsProviderBase::useHttpAsyncClient() {
  return Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.use_http_client_to_fetch_aws_credentials");
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

InstanceProfileCredentialsProvider::InstanceProfileCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name)
    : MetadataCredentialsProviderBase(
          api, context, fetch_metadata_using_curl, create_metadata_fetcher_cb, cluster_name,
          envoy::config::cluster::v3::Cluster::STATIC /*cluster_type*/, EC2_METADATA_HOST) {}

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

  if (!useHttpAsyncClient() || !context_) {
    // Using curl to fetch the AWS credentials where we first get the token.
    const auto token_string = fetch_metadata_using_curl_(token_req_message);
    if (token_string) {
      ENVOY_LOG(debug, "Obtained token to make secure call to EC2MetadataService");
      fetchInstanceRole(std::move(token_string.value()));
    } else {
      ENVOY_LOG(warn,
                "Failed to get token from EC2MetadataService, falling back to less secure way");
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
    continue_on_async_fetch_failure_reason_ = "Token fetch failed so fall back to less secure way";
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
    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void InstanceProfileCredentialsProvider::fetchCredentialFromInstanceRole(
    const std::string&& instance_role, const std::string&& token_string,
    bool async /*default = false*/) {

  if (instance_role.empty()) {
    ENVOY_LOG(error, "No roles found to fetch AWS credentials from the EC2MetadataService");
    if (async) {
      handleFetchDone();
    }
    return;
  }
  const auto instance_role_list = StringUtil::splitToken(StringUtil::trim(instance_role), "\n");
  if (instance_role_list.empty()) {
    ENVOY_LOG(error, "No roles found to fetch AWS credentials from the EC2MetadataService");
    if (async) {
      handleFetchDone();
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
    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void InstanceProfileCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value, bool async /*default = false*/) {
  if (credential_document_value.empty()) {
    if (async) {
      handleFetchDone();
    }
    return;
  }
  Json::ObjectSharedPtr document_json;
  TRY_NEEDS_AUDIT { document_json = Json::Factory::loadFromString(credential_document_value); }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document: {}", e.what());
    if (async) {
      handleFetchDone();
    }
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

  last_updated_ = api_.timeSource().systemTime();
  if (useHttpAsyncClient() && context_) {
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
  } else {
    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  handleFetchDone();
}

void InstanceProfileCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  // TODO(suniltheta): increment fetch success stats
  ENVOY_LOG(debug, "AWS Instance metadata fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void InstanceProfileCredentialsProvider::onMetadataError(Failure reason) {
  // TODO(suniltheta): increment fetch failed stats
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

TaskRoleCredentialsProvider::TaskRoleCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view credential_uri,
    absl::string_view authorization_token = {}, absl::string_view cluster_name = {})
    : MetadataCredentialsProviderBase(
          api, context, fetch_metadata_using_curl, create_metadata_fetcher_cb, cluster_name,
          envoy::config::cluster::v3::Cluster::STATIC /*cluster_type*/, credential_uri),
      credential_uri_(credential_uri), authorization_token_(authorization_token) {}

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
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(host);
  message.headers().setPath(path);
  message.headers().setCopy(Http::CustomHeaders::get().Authorization, authorization_token_);
  if (!useHttpAsyncClient() || !context_) {
    // Using curl to fetch the AWS credentials.
    const auto credential_document = fetch_metadata_using_curl_(message);
    if (!credential_document) {
      ENVOY_LOG(error, "Could not load AWS credentials document from the task role");
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
    metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
  }
}

void TaskRoleCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  if (credential_document_value.empty()) {
    handleFetchDone();
    return;
  }
  Json::ObjectSharedPtr document_json;
  TRY_NEEDS_AUDIT { document_json = Json::Factory::loadFromString(credential_document_value); }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from the task role: {}", e.what());
    handleFetchDone();
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
  if (useHttpAsyncClient() && context_) {
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
  } else {
    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  handleFetchDone();
}

void TaskRoleCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  // TODO(suniltheta): increment fetch success stats
  ENVOY_LOG(debug, "AWS Task metadata fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void TaskRoleCredentialsProvider::onMetadataError(Failure reason) {
  // TODO(suniltheta): increment fetch failed stats
  ENVOY_LOG(error, "AWS metadata fetch failure: {}", metadata_fetcher_->failureToString(reason));
  handleFetchDone();
}

WebIdentityCredentialsProvider::WebIdentityCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context,
    const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view token_file_path,
    absl::string_view sts_endpoint, absl::string_view role_arn, absl::string_view role_session_name,
    absl::string_view cluster_name = {})
    : MetadataCredentialsProviderBase(
          api, context, fetch_metadata_using_curl, create_metadata_fetcher_cb, cluster_name,
          envoy::config::cluster::v3::Cluster::LOGICAL_DNS /*cluster_type*/, sts_endpoint),
      token_file_path_(token_file_path), sts_endpoint_(sts_endpoint), role_arn_(role_arn),
      role_session_name_(role_session_name) {}

bool WebIdentityCredentialsProvider::needsRefresh() {
  const auto now = api_.timeSource().systemTime();
  return (now - last_updated_ > REFRESH_INTERVAL) ||
         (expiration_time_ - now < REFRESH_GRACE_PERIOD);
}

void WebIdentityCredentialsProvider::refresh() {
  // If http async client is not enabled then just set empty credentials and return.
  if (!useHttpAsyncClient()) {
    cached_credentials_ = Credentials();
    return;
  }

  ENVOY_LOG(debug, "Getting AWS web identity credentials from STS: {}", sts_endpoint_);

  const auto web_token_file_or_error = api_.fileSystem().fileReadToEnd(token_file_path_);
  THROW_IF_STATUS_NOT_OK(web_token_file_or_error, throw);
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(Http::Utility::parseAuthority(sts_endpoint_).host_);
  message.headers().setPath(
      fmt::format("/?Action=AssumeRoleWithWebIdentity"
                  "&Version=2011-06-15"
                  "&RoleSessionName={}"
                  "&RoleArn={}"
                  "&WebIdentityToken={}",
                  Envoy::Http::Utility::PercentEncoding::encode(role_session_name_),
                  Envoy::Http::Utility::PercentEncoding::encode(role_arn_),
                  Envoy::Http::Utility::PercentEncoding::encode(web_token_file_or_error.value())));
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
  metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
}

void WebIdentityCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  if (credential_document_value.empty()) {
    handleFetchDone();
    ENVOY_LOG(error, "Could not load AWS credentials document from STS");
    return;
  }

  Json::ObjectSharedPtr document_json;
  TRY_NEEDS_AUDIT { document_json = Json::Factory::loadFromString(credential_document_value); }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from STS: {}", e.what());
    handleFetchDone();
    return;
  }

  absl::StatusOr<Json::ObjectSharedPtr> root_node =
      document_json->getObjectNoThrow(WEB_IDENTITY_RESPONSE_ELEMENT);
  if (!root_node.ok()) {
    ENVOY_LOG(error, "AWS STS credentials document is empty");
    handleFetchDone();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> result_node =
      root_node.value()->getObjectNoThrow(WEB_IDENTITY_RESULT_ELEMENT);
  if (!result_node.ok()) {
    ENVOY_LOG(error, "AWS STS returned an unexpected result");
    handleFetchDone();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> credentials =
      result_node.value()->getObjectNoThrow(CREDENTIALS);
  if (!credentials.ok()) {
    ENVOY_LOG(error, "AWS STS credentials document does not contain any credentials");
    handleFetchDone();
    return;
  }

  TRY_NEEDS_AUDIT {
    const auto access_key_id = credentials.value()->getString(ACCESS_KEY_ID, "");
    const auto secret_access_key = credentials.value()->getString(SECRET_ACCESS_KEY, "");
    const auto session_token = credentials.value()->getString(SESSION_TOKEN, "");

    ENVOY_LOG(debug, "Received the following AWS credentials from STS: {}={}, {}={}, {}={}",
              AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
              secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
              session_token.empty() ? "" : "*****");
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
  }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Bad format, could not parse AWS credentials document from STS: {}", e.what());
    handleFetchDone();
    return;
  }

  TRY_NEEDS_AUDIT {
    const auto expiration = credentials.value()->getInteger(EXPIRATION, 0);
    if (expiration != 0) {
      expiration_time_ =
          std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(expiration));
      ENVOY_LOG(debug, "AWS STS credentials expiration time (unix timestamp): {}", expiration);
    } else {
      expiration_time_ = api_.timeSource().systemTime() + REFRESH_INTERVAL;
      ENVOY_LOG(warn, "Could not get Expiration value of AWS credentials document from STS, so "
                      "setting expiration to 1 hour in future");
    }
  }
  END_TRY catch (EnvoyException& e) {
    expiration_time_ = api_.timeSource().systemTime() + REFRESH_INTERVAL;
    ENVOY_LOG(warn,
              "Could not parse Expiration value of AWS credentials document from STS: {}, so "
              "setting expiration to 1 hour in future",
              e.what());
  }

  last_updated_ = api_.timeSource().systemTime();
  handleFetchDone();
}

void WebIdentityCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  // TODO(suniltheta): increment fetch success stats
  ENVOY_LOG(debug, "AWS metadata fetch from STS success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void WebIdentityCredentialsProvider::onMetadataError(Failure reason) {
  // TODO(suniltheta): increment fetch failed stats
  ENVOY_LOG(error, "AWS metadata fetch failure: {}", metadata_fetcher_->failureToString(reason));
  handleFetchDone();
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
    Api::Api& api, ServerFactoryContextOptRef context, absl::string_view region,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    const CredentialsProviderChainFactories& factories) {
  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_aws_credentials_file")) {
    ENVOY_LOG(debug, "Using credentials file credentials provider");
    add(factories.createCredentialsFileCredentialsProvider(api));
  } else {
    ENVOY_LOG(debug, "Not using credential file credentials provider because it is not enabled");
  }

  // WebIdentityCredentialsProvider can be used only if `context` is supplied which is required to
  // use http async http client to make http calls to fetch the credentials.
  if (context) {
    const auto web_token_path = absl::NullSafeStringView(std::getenv(AWS_WEB_IDENTITY_TOKEN_FILE));
    const auto role_arn = absl::NullSafeStringView(std::getenv(AWS_ROLE_ARN));
    if (!web_token_path.empty() && !role_arn.empty()) {
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
      const auto sts_endpoint = Utility::getSTSEndpoint(region) + ":443";
      ENVOY_LOG(
          debug,
          "Using web identity credentials provider with STS endpoint: {} and session name: {}",
          sts_endpoint, actual_session_name);
      add(factories.createWebIdentityCredentialsProvider(
          api, context, fetch_metadata_using_curl, MetadataFetcher::create, STS_TOKEN_CLUSTER,
          web_token_path, sts_endpoint, role_arn, actual_session_name));
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
    ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", uri);
    add(factories.createTaskRoleCredentialsProvider(api, context, fetch_metadata_using_curl,
                                                    MetadataFetcher::create,
                                                    CONTAINER_METADATA_CLUSTER, uri));
  } else if (!full_uri.empty()) {
    const auto authorization_token =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
    if (!authorization_token.empty()) {
      ENVOY_LOG(debug,
                "Using task role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createTaskRoleCredentialsProvider(
          api, context, fetch_metadata_using_curl, MetadataFetcher::create,
          CONTAINER_METADATA_CLUSTER, full_uri, authorization_token));
    } else {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, context, fetch_metadata_using_curl,
                                                      MetadataFetcher::create,
                                                      CONTAINER_METADATA_CLUSTER, full_uri));
    }
  } else if (metadata_disabled != TRUE) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(
        api, context, fetch_metadata_using_curl, MetadataFetcher::create, EC2_METADATA_CLUSTER));
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
