#include "source/extensions/common/aws/credential_providers/container_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
