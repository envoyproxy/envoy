#include "source/extensions/common/aws/credentials_provider_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/lock_guard.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "source/extensions/common/aws/utility.h"

#include "tinyxml2.h"

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

constexpr char WEB_IDENTITY_RESULT_ELEMENT[] = "AssumeRoleWithWebIdentityResult";
constexpr char CREDENTIALS[] = "Credentials";
constexpr char ACCESS_KEY_ID[] = "AccessKeyId";
constexpr char SECRET_ACCESS_KEY[] = "SecretAccessKey";
constexpr char TOKEN[] = "Token";
constexpr char SESSION_TOKEN[] = "SessionToken";
constexpr char EXPIRATION[] = "Expiration";
constexpr char EXPIRATION_FORMAT[] = "%E4Y%m%dT%H%M%S%z";
constexpr char STS_EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";
constexpr char TRUE[] = "true";

constexpr char AWS_CONTAINER_CREDENTIALS_RELATIVE_URI[] = "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI";
constexpr char AWS_CONTAINER_CREDENTIALS_FULL_URI[] = "AWS_CONTAINER_CREDENTIALS_FULL_URI";
constexpr char AWS_CONTAINER_AUTHORIZATION_TOKEN[] = "AWS_CONTAINER_AUTHORIZATION_TOKEN";
constexpr char AWS_EC2_METADATA_DISABLED[] = "AWS_EC2_METADATA_DISABLED";

constexpr std::chrono::hours REFRESH_INTERVAL{1};
constexpr std::chrono::seconds REFRESH_GRACE_PERIOD{5};
constexpr char EC2_METADATA_HOST[] = "169.254.169.254:80";
constexpr char CONTAINER_METADATA_HOST[] = "169.254.170.2:80";
constexpr char SECURITY_CREDENTIALS_PATH[] = "/latest/meta-data/iam/security-credentials";

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

void MetadataCredentialsProviderBase::refreshIfNeeded() {
  const Thread::LockGuard lock(lock_);
  if (needsRefresh()) {
    refresh();
  }
}

bool InstanceProfileCredentialsProvider::needsRefresh() {
  return api_.timeSource().systemTime() - last_updated_ > REFRESH_INTERVAL;
}

void InstanceProfileCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS credentials from the instance metadata");

  // First discover the Role of this instance
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(EC2_METADATA_HOST);
  message.headers().setPath(SECURITY_CREDENTIALS_PATH);
  const auto instance_role_string = metadata_fetcher_(message);
  if (!instance_role_string) {
    ENVOY_LOG(error, "Could not retrieve credentials listing from the instance metadata");
    return;
  }

  const auto instance_role_list =
      StringUtil::splitToken(StringUtil::trim(instance_role_string.value()), "\n");
  if (instance_role_list.empty()) {
    ENVOY_LOG(error, "No AWS credentials were found in the instance metadata");
    return;
  }
  ENVOY_LOG(debug, "AWS credentials list:\n{}", instance_role_string.value());

  // Only one Role can be associated with an instance:
  // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html
  const auto credential_path =
      std::string(SECURITY_CREDENTIALS_PATH) + "/" +
      std::string(instance_role_list[0].data(), instance_role_list[0].size());
  ENVOY_LOG(debug, "AWS credentials path: {}", credential_path);

  // Then fetch and parse the credentials
  message.headers().setPath(credential_path);
  const auto credential_document = metadata_fetcher_(message);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the instance metadata");
    return;
  }

  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document: {}", e.what());
    return;
  }

  const auto access_key_id = document_json->getString(ACCESS_KEY_ID, "");
  const auto secret_access_key = document_json->getString(SECRET_ACCESS_KEY, "");
  const auto session_token = document_json->getString(TOKEN, "");

  ENVOY_LOG(debug, "Found following AWS credentials in the instance metadata: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  last_updated_ = api_.timeSource().systemTime();
}

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
  const auto credential_document = metadata_fetcher_(message);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from the task role");
    return;
  }

  Json::ObjectSharedPtr document_json;
  try {
    document_json = Json::Factory::loadFromString(credential_document.value());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from the task role: {}", e.what());
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
  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
}

bool WebIdentityCredentialsProvider::needsRefresh() {
  const auto now = api_.timeSource().systemTime();
  return (now - last_updated_ > REFRESH_INTERVAL) ||
         (expiration_time_ - now < REFRESH_GRACE_PERIOD);
}

void WebIdentityCredentialsProvider::refresh() {
  ENVOY_LOG(debug, "Getting AWS web identity credentials from STS: {}", sts_endpoint_);

  const auto web_token = api_.fileSystem().fileReadToEnd(token_file_path_);

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Https);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost(sts_endpoint_);
  message.headers().setPath(
      fmt::format("/?Action=AssumeRoleWithWebIdentity"
                  "&Version=2011-06-15"
                  "&RoleSessionName={}"
                  "&RoleArn={}"
                  "&WebIdentityToken={}",
                  Envoy::Http::Utility::PercentEncoding::encode(role_session_name_),
                  Envoy::Http::Utility::PercentEncoding::encode(role_arn_),
                  Envoy::Http::Utility::PercentEncoding::encode(web_token)));

  const auto credential_document = metadata_fetcher_(message);
  if (!credential_document) {
    ENVOY_LOG(error, "Could not load AWS credentials document from STS");
    return;
  }

  tinyxml2::XMLDocument document_xml;
  if (document_xml.Parse(credential_document->c_str()) != tinyxml2::XML_SUCCESS) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from STS: {}",
              document_xml.ErrorStr());
    return;
  }

  const auto* root = document_xml.RootElement();
  if (root == nullptr) {
    ENVOY_LOG(error, "AWS STS credentials document is empty");
    return;
  }

  const tinyxml2::XMLElement* result_element = root;
  // The result may be wrapped in another container, so recurse one level just in case.
  if (absl::string_view(root->Name()) != WEB_IDENTITY_RESULT_ELEMENT) {
    result_element = root->FirstChildElement(WEB_IDENTITY_RESULT_ELEMENT);
  }

  if (result_element == nullptr) {
    ENVOY_LOG(error, "AWS STS returned an unexpected result");
    return;
  }

  const auto credentials = result_element->FirstChildElement(CREDENTIALS);
  if (credentials == nullptr) {
    ENVOY_LOG(error, "AWS STS credentials document does not contain any credentials");
    return;
  }

  const auto access_key_id_element = credentials->FirstChildElement(ACCESS_KEY_ID);
  const std::string access_key_id =
      access_key_id_element == nullptr ? "" : access_key_id_element->GetText();

  const auto secret_access_key_element = credentials->FirstChildElement(SECRET_ACCESS_KEY);
  const std::string secret_access_key =
      secret_access_key_element == nullptr ? "" : secret_access_key_element->GetText();

  const auto session_token_element = credentials->FirstChildElement(SESSION_TOKEN);
  const std::string session_token =
      session_token_element == nullptr ? "" : session_token_element->GetText();

  ENVOY_LOG(debug, "Received the following AWS credentials from STS: {}={}, {}={}, {}={}",
            AWS_ACCESS_KEY_ID, access_key_id, AWS_SECRET_ACCESS_KEY,
            secret_access_key.empty() ? "" : "*****", AWS_SESSION_TOKEN,
            session_token.empty() ? "" : "*****");

  const auto expiration_element = credentials->FirstChildElement(EXPIRATION);
  const std::string expiration_str =
      expiration_element == nullptr ? "" : expiration_element->GetText();
  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(STS_EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "AWS STS credentials expiration time: {}", expiration_str);
      expiration_time_ = absl::ToChronoTime(expiration_time);
    }
  }

  last_updated_ = api_.timeSource().systemTime();
  cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
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
    Api::Api& api, absl::string_view region,
    const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher,
    const CredentialsProviderChainFactories& factories) {
  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());

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
    const auto sts_endpoint = Utility::getSTSEndpoint(region);
    ENVOY_LOG(debug,
              "Using web identity credentials provider with STS endpoint: {} and session name: {}",
              sts_endpoint, actual_session_name);
    add(factories.createWebIdentityCredentialsProvider(
        api, metadata_fetcher, web_token_path, sts_endpoint, role_arn, actual_session_name));
  }

  const auto relative_uri =
      absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI));
  const auto full_uri = absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI));
  const auto metadata_disabled = absl::NullSafeStringView(std::getenv(AWS_EC2_METADATA_DISABLED));

  if (!relative_uri.empty()) {
    const auto uri = absl::StrCat(CONTAINER_METADATA_HOST, relative_uri);
    ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", uri);
    add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, uri));
  } else if (!full_uri.empty()) {
    const auto authorization_token =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
    if (!authorization_token.empty()) {
      ENVOY_LOG(debug,
                "Using task role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri,
                                                      authorization_token));
    } else {
      ENVOY_LOG(debug, "Using task role credentials provider with URI: {}", full_uri);
      add(factories.createTaskRoleCredentialsProvider(api, metadata_fetcher, full_uri));
    }
  } else if (metadata_disabled != TRUE) {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(api, metadata_fetcher));
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
