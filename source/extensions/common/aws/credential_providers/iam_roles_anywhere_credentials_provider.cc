#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_credentials_provider.h"

#include "envoy/server/factory_context.h"

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

namespace {
// IAM Roles Anywhere credential strings
constexpr absl::string_view CREDENTIAL_SET = "credentialSet";
constexpr absl::string_view CREDENTIALS_LOWER = "credentials";
constexpr absl::string_view ACCESS_KEY_ID_LOWER = "accessKeyId";
constexpr absl::string_view SECRET_ACCESS_KEY_LOWER = "secretAccessKey";
constexpr absl::string_view EXPIRATION_LOWER = "expiration";
constexpr absl::string_view SESSION_TOKEN_LOWER = "sessionToken";
} // namespace

IAMRolesAnywhereCredentialsProvider::IAMRolesAnywhereCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view cluster_name, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    absl::string_view region, MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer,
    std::unique_ptr<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer> roles_anywhere_signer,
    const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider
        iam_roles_anywhere_config)

    : MetadataCredentialsProviderBase(context.api(), context, aws_cluster_manager, cluster_name,
                                      create_metadata_fetcher_cb, refresh_state,
                                      initialization_timer),
      role_arn_(iam_roles_anywhere_config.role_arn()),
      role_session_name_(iam_roles_anywhere_config.role_session_name()),
      profile_arn_(iam_roles_anywhere_config.profile_arn()),
      trust_anchor_arn_(iam_roles_anywhere_config.trust_anchor_arn()), region_(region),
      session_duration_(PROTOBUF_GET_SECONDS_OR_DEFAULT(
          iam_roles_anywhere_config, session_duration,
          Extensions::Common::Aws::IAMRolesAnywhereSignatureConstants::DefaultExpiration)),
      roles_anywhere_signer_(std::move(roles_anywhere_signer)) {}

void IAMRolesAnywhereCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS IAM Roles Anywhere fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void IAMRolesAnywhereCredentialsProvider::onMetadataError(Failure reason) {
  stats_->credential_refreshes_failed_.inc();
  ENVOY_LOG(error, "AWS IAM Roles Anywhere fetch failure: {}",
            metadata_fetcher_->failureToString(reason));
  credentialsRetrievalError();
}

void IAMRolesAnywhereCredentialsProvider::refresh() {

  const auto uri = aws_cluster_manager_->getUriFromClusterName(cluster_name_);
  if (!uri.ok()) {
    ENVOY_LOG(error, "AWS Cluster Manager Unable to find cluster {}", cluster_name_);
    credentialsRetrievalError();
    return;
  }

  ENVOY_LOG(debug, "Getting AWS credentials from the rolesanywhere service at URI: {}",
            uri.value());

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Post);
  message.headers().setHost(Http::Utility::parseAuthority(uri.value()).host_);
  message.headers().setPath("/sessions");
  message.headers().setContentType("application/json");

  auto json_message = ProtobufWkt::Struct();
  auto& fields = *json_message.mutable_fields();
  fields["profileArn"].set_string_value(profile_arn_);
  fields["roleArn"].set_string_value(role_arn_);
  fields["trustAnchorArn"].set_string_value(trust_anchor_arn_);

  if (session_duration_.has_value()) {
    fields["durationSeconds"].set_number_value(session_duration_.value());
  }
  if (!role_session_name_.empty()) {
    fields["roleSessionName"].set_string_value(role_session_name_);
  }

  auto body_data = Json::Factory::loadFromProtobufStruct(json_message);

  message.body().add(body_data->asJsonString());
  ENVOY_LOG(debug, "IAM Roles Anywhere /sessions payload: {}", body_data->asJsonString());

  const auto status = roles_anywhere_signer_->sign(message, true, region_);
  if (!status.ok()) {
    ENVOY_LOG(debug, status.message());
    credentialsRetrievalError();
    return;
  }
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

void IAMRolesAnywhereCredentialsProvider::extractCredentials(
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
      document_json_or_error.value()->getObjectArray(std::string(CREDENTIAL_SET), false);
  if (!credentialset_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credentialset_object_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  // We only consider the first credential returned in a CredentialSet
  auto credential_object_or_error =
      credentialset_object_or_error.value()[0]->getObject(std::string(CREDENTIALS_LOWER));
  if (!credential_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credential_object_or_error.status().message());
    credentialsRetrievalError();
    return;
  }

  const auto access_key_id = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), std::string(ACCESS_KEY_ID_LOWER), "");
  const auto secret_access_key = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), std::string(SECRET_ACCESS_KEY_LOWER), "");
  const auto session_token = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), std::string(SESSION_TOKEN_LOWER), "");

  ENVOY_LOG(debug,
            "Found following AWS credentials from rolesanywhere service: {}={}, {}={}, {}={}",
            ACCESS_KEY_ID_LOWER, access_key_id, SECRET_ACCESS_KEY_LOWER,
            secret_access_key.empty() ? "" : "*****", SESSION_TOKEN_LOWER,
            session_token.empty() ? "" : "*****");

  const auto expiration_str = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), std::string(EXPIRATION_LOWER), "");

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
