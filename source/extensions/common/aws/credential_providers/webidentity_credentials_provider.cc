#include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

WebIdentityCredentialsProvider::WebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view cluster_name, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config)
    : MetadataCredentialsProviderBase(context, aws_cluster_manager, cluster_name,
                                      create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
      role_arn_(web_identity_config.role_arn()),
      role_session_name_(web_identity_config.role_session_name()) {

  auto provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      web_identity_config.web_identity_token_data_source(), context.mainThreadDispatcher(),
      context.threadLocal(), context.api(), false, 4096);
  if (provider_or_error_.ok()) {
    web_identity_data_source_provider_ = std::move(provider_or_error_.value());
  } else {
    ENVOY_LOG(info, "Invalid web identity data source");
    web_identity_data_source_provider_.reset();
  }
}

void WebIdentityCredentialsProvider::refresh() {

  absl::string_view web_identity_data;

  // If we're unable to read from the configured data source, exit early.
  if (!web_identity_data_source_provider_.has_value()) {
    return;
  }

  ENVOY_LOG(debug, "Getting AWS web identity credentials from STS: {}",
            aws_cluster_manager_->getUriFromClusterName(cluster_name_).value());
  web_identity_data = web_identity_data_source_provider_.value()->data();

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  auto statusOr = aws_cluster_manager_->getUriFromClusterName(cluster_name_);
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

void WebIdentityCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {

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

  const auto expiration =
      Utility::getIntegerFromJsonOrDefault(credentials.value(), WEB_IDENTITY_EXPIRATION, 0);

  if (expiration != 0) {
    expiration_time_ =
        std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(expiration));
    ENVOY_LOG(debug, "AWS STS credentials expiration time (unix timestamp): {}", expiration);
  } else {
    // We don't have a valid expiration time from the json response
    expiration_time_.reset();
  }

  last_updated_ = context_.api().timeSource().systemTime();
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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
