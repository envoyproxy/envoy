#include "source/extensions/common/aws/credential_providers/instance_profile_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
