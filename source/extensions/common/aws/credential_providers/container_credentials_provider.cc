#include "source/extensions/common/aws/credential_providers/container_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

ContainerCredentialsProvider::ContainerCredentialsProvider(
    Api::Api& api, Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    absl::string_view credential_uri, MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view authorization_token,
    absl::string_view cluster_name)
    : MetadataCredentialsProviderBase(api, context, aws_cluster_manager, cluster_name,
                                      create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
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

  ENVOY_LOG(debug, "Getting AWS credentials from the container role at URI: {}",
            aws_cluster_manager_->getUriFromClusterName(cluster_name_).value());
  Http::Utility::extractHostPathFromUri(
      aws_cluster_manager_->getUriFromClusterName(cluster_name_).value(), host, path);

  // ECS Task role: use const authorization_token set during initialization
  absl::string_view authorization_header = authorization_token_;
  absl::StatusOr<std::string> token_or_error;

  if (authorization_token_.empty()) {
    // EKS Pod Identity token is sourced from AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE
    if (const auto token_file = std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE)) {
      token_or_error = api_.fileSystem().fileReadToEnd(std::string(token_file));
      if (token_or_error.ok()) {
        ENVOY_LOG(debug, "Container authorization token file contents loaded");
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
  // Stop any existing timer.
  if (cache_duration_timer_ && cache_duration_timer_->enabled()) {
    cache_duration_timer_->disableTimer();
  }
  // Using Http async client to fetch the AWS credentials.
  if (!metadata_fetcher_) {
    metadata_fetcher_ = create_metadata_fetcher_cb_(context_.clusterManager(), clusterName());
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
            ACCESS_KEY_ID, access_key_id, SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", TOKEN, session_token.empty() ? "" : "*****");

  const auto expiration_str =
      Utility::getStringFromJsonOrDefault(document_json_or_error.value(), CONTAINER_EXPIRATION, "");

  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "Container role AWS credentials expiration time: {}", expiration_str);
      expiration_time_ = absl::ToChronoTime(expiration_time);
    }
  }

  last_updated_ = api_.timeSource().systemTime();
  setCredentialsToAllThreads(
      std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
  stats_->credential_refreshes_succeeded_.inc();

  ENVOY_LOG(debug, "Metadata receiver {} moving to Ready state", cluster_name_);
  refresh_state_ = MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  // Set receiver state in statistics
  stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));
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
