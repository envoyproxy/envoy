#include "source/extensions/common/aws/credential_providers/assume_role_credentials_provider.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/utility.h"


namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
using std::chrono::seconds;

AssumeRoleCredentialsProvider::AssumeRoleCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerPtr aws_cluster_manager, absl::string_view cluster_name,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view region,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer,
    std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer,
    envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider
        assume_role_config)

    : MetadataCredentialsProviderBase(context.api(), context, aws_cluster_manager, cluster_name,
                                      create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
      role_arn_(assume_role_config.role_arn()),
      role_session_name_(assume_role_config.role_session_name()),
      region_(region),
      server_factory_context_(context), assume_role_signer_(std::move(assume_role_signer)) {

      if(assume_role_config.has_session_duration())
      {
        session_duration_ = DurationUtil::durationToSeconds(assume_role_config.session_duration());
      }
}

void AssumeRoleCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS IAM Roles Anywhere fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void AssumeRoleCredentialsProvider::onMetadataError(Failure reason) {
  stats_->credential_refreshes_failed_.inc();
  ENVOY_LOG(error, "AWS IAM Roles Anywhere  fetch failure: {}",
            metadata_fetcher_->failureToString(reason));
  credentialsRetrievalError();
}

// TODO: @nbaws Unused and will be removed when curl is deprecated
bool AssumeRoleCredentialsProvider::needsRefresh() { return true; }

void AssumeRoleCredentialsProvider::refresh() {

  const auto uri = aws_cluster_manager_.ref()->getUriFromClusterName(cluster_name_);
  ENVOY_LOG(debug, "Getting AWS credentials from the rolesanywhere service at URI: {}",
            uri.value());

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Post);
  message.headers().setHost(Http::Utility::parseAuthority(uri.value()).host_);
  message.headers().setPath("/sessions");
  message.headers().setContentType("application/json");

  std::string body_data;
  body_data.append("{");
  if (session_duration_.has_value()) {
    body_data.append(fmt::format("\"durationSeconds\": {}, ", session_duration_.value()));
  }
  body_data.append(fmt::format("\"roleArn\": \"{}\", ", role_arn_));
  if (!role_session_name_.empty()) {
    body_data.append(fmt::format(", \"roleSessionName\": \"{}\"", role_session_name_));
  }
  body_data.append("}");
  message.body().add(body_data);
  ENVOY_LOG(debug, "AssumeRole  payload: {}", body_data);

  auto status = assume_role_signer_->sign(message, true, region_);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, status.message());
    credentialsRetrievalError();
    return;
  }
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

void AssumeRoleCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  absl::StatusOr<Json::ObjectSharedPtr> document_json_or_error;

  document_json_or_error = Json::Factory::loadFromString(credential_document_value);
  if (!document_json_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              document_json_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  auto credentialset_object_or_error =
      document_json_or_error.value()->getObjectArray(CREDENTIAL_SET, false);
  if (!credentialset_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credentialset_object_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  // We only consider the first credential returned in a CredentialSet
  auto credential_object_or_error =
      credentialset_object_or_error.value()[0]->getObject(CREDENTIALS_LOWER);
  if (!credential_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credential_object_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  const auto access_key_id = Utility::getStringFromJsonOrDefault(credential_object_or_error.value(),
                                                                 ACCESS_KEY_ID_LOWER, "");
  const auto secret_access_key = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), SECRET_ACCESS_KEY_LOWER, "");
  const auto session_token = Utility::getStringFromJsonOrDefault(credential_object_or_error.value(),
                                                                 SESSION_TOKEN_LOWER, "");

  ENVOY_LOG(debug,
            "Found following AWS credentials from rolesanywhere service: {}={}, {}={}, {}={}",
            ACCESS_KEY_ID_LOWER, access_key_id, SECRET_ACCESS_KEY_LOWER,
            secret_access_key.empty() ? "" : "*****", SESSION_TOKEN_LOWER,
            session_token.empty() ? "" : "*****");

  const auto expiration_str =
      Utility::getStringFromJsonOrDefault(credential_object_or_error.value(), EXPIRATION_LOWER, "");

  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "Rolesanywhere role AWS credentials expiration time: {}", expiration_str);
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

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
