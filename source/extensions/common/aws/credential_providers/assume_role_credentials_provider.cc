#include "source/extensions/common/aws/credential_providers/assume_role_credentials_provider.h"

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
using std::chrono::seconds;

AssumeRoleCredentialsProvider::AssumeRoleCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view cluster_name, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    absl::string_view region, MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer,
    std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer,
    envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider assume_role_config)

    : MetadataCredentialsProviderBase(context, aws_cluster_manager, cluster_name,
                                      create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
      role_arn_(assume_role_config.role_arn()),
      role_session_name_(assume_role_config.role_session_name()), region_(region),
      external_id_(assume_role_config.external_id()),
      assume_role_signer_(std::move(assume_role_signer)) {

  if (assume_role_config.has_session_duration()) {
    session_duration_ = DurationUtil::durationToSeconds(assume_role_config.session_duration());
  }
}

void AssumeRoleCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS STS AssumeRole fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void AssumeRoleCredentialsProvider::onMetadataError(Failure reason) {
  stats_->credential_refreshes_failed_.inc();
  ENVOY_LOG(error, "AWS STS AssumeRole fetch failure: {}",
            metadata_fetcher_->failureToString(reason));
  credentialsRetrievalError();
}

void AssumeRoleCredentialsProvider::refresh() {
  // We can have assume role credentials pending at this point, as the signers credential provider
  // chain is potentially async
  if (assume_role_signer_->addCallbackIfCredentialsPending(CancelWrapper::cancelWrapped(
          [this]() { continueRefresh(); }, &cancel_refresh_callback_)) == false) {
    // We're not pending credentials, so sign immediately
    return continueRefresh();
  } else {
    // Leave and let our callback handle the rest of the processing
    return;
  }
}

void AssumeRoleCredentialsProvider::continueRefresh() {
  const auto uri = aws_cluster_manager_->getUriFromClusterName(cluster_name_);
  ENVOY_LOG(debug, "Getting AWS credentials from STS at URI: {}", uri.value());

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(Http::Utility::parseAuthority(uri.value()).host_);
  std::string path =
      fmt::format("/?Version=2011-06-15&Action=AssumeRole&RoleArn={}&RoleSessionName={}",
                  Envoy::Http::Utility::PercentEncoding::encode(role_arn_),
                  Envoy::Http::Utility::PercentEncoding::encode(role_session_name_));
  if (session_duration_) {
    path += fmt::format("&DurationSeconds={}", session_duration_.value());
  }

  if (!external_id_.empty()) {
    path +=
        fmt::format("&ExternalId={}", Envoy::Http::Utility::PercentEncoding::encode(external_id_));
  }

  message.headers().setPath(path);
  // Use the Accept header to ensure that AssumeRoleResponse is returned as JSON.
  message.headers().setReference(Http::CustomHeaders::get().Accept,
                                 Http::Headers::get().ContentTypeValues.Json);

  // No code path exists that can cause signing to fail, as signing only fails if path or method is
  // unset.
  auto status = assume_role_signer_->sign(message, true, region_);

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

void AssumeRoleCredentialsProvider::extractCredentials(
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
      document_json_or_error.value()->getObject(ASSUMEROLE_RESPONSE_ELEMENT);
  if (!root_node.ok()) {
    ENVOY_LOG(error, "AWS STS credentials document is empty");
    credentialsRetrievalError();
    return;
  }
  absl::StatusOr<Json::ObjectSharedPtr> result_node =
      root_node.value()->getObject(ASSUMEROLE_RESULT_ELEMENT);
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

  last_updated_ = context_.api().timeSource().systemTime();
  handleFetchDone();
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
