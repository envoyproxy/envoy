#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/credential_providers/assume_role_credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::_;
using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MessageMatcher : public testing::MatcherInterface<Http::RequestMessage&> {
public:
  explicit MessageMatcher(const Http::TestRequestHeaderMapImpl& expected_headers)
      : expected_headers_(expected_headers) {}

  bool MatchAndExplain(Http::RequestMessage& message,
                       testing::MatchResultListener* result_listener) const override {
    const bool equal = TestUtility::headerMapEqualIgnoreOrder(message.headers(), expected_headers_);
    if (!equal) {
      *result_listener << "\n"
                       << TestUtility::addLeftAndRightPadding("Expected header map:") << "\n"
                       << expected_headers_
                       << TestUtility::addLeftAndRightPadding("is not equal to actual header map:")
                       << "\n"
                       << message.headers()
                       << TestUtility::addLeftAndRightPadding("") // line full of padding
                       << "\n";
    }
    return equal;
  }

  void DescribeTo(::std::ostream* os) const override { *os << "Message matches"; }

  void DescribeNegationTo(::std::ostream* os) const override { *os << "Message does not match"; }

private:
  const Http::TestRequestHeaderMapImpl expected_headers_;
};

testing::Matcher<Http::RequestMessage&>
messageMatches(const Http::TestRequestHeaderMapImpl& expected_headers) {
  return testing::MakeMatcher(new MessageMatcher(expected_headers));
}

class AssumeRoleCredentialsProviderTest : public testing::Test {
  // };
public:
  AssumeRoleCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void setupProvider(MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
                         MetadataFetcher::MetadataReceiver::RefreshState::Ready,
                     std::chrono::seconds initialization_timer = std::chrono::seconds(2)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    std::string token_file_path;
    envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider cred_provider = {};

    cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
    cred_provider.set_role_session_name("role-session-name");

    mock_manager_ = std::make_shared<MockAwsClusterManager>();

    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("sts.region.amazonaws.com:443"));

    auto cluster_name = "credentials_provider_cluster";
    envoy::extensions::common::aws::v3::AwsCredentialProvider defaults;
    envoy::extensions::common::aws::v3::EnvironmentCredentialProvider env_provider;
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
    TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);

    defaults.mutable_environment_credential_provider()->CopyFrom(env_provider);

    auto credentials_provider_chain =
        std::make_shared<Extensions::Common::Aws::CommonCredentialsProviderChain>(
            context_, "region", defaults);

    auto signer = std::make_unique<SigV4SignerImpl>(
        STS_SERVICE_NAME, "region", credentials_provider_chain, context_,
        Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
        Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<AssumeRoleCredentialsProvider>(
        context_, mock_manager_, cluster_name,
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        "region", refresh_state, initialization_timer, std::move(signer), cred_provider);
  }

  void expectDocument(const uint64_t status_code, const std::string&& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/?Version=2011-06-15&Action=AssumeRole&RoleArn=aws:iam::123456789012:role/"
                  "arn&RoleSessionName=role-session-name"},
        {":authority", "sts.region.amazonaws.com"},
        {":scheme", "https"},
        {":method", "GET"},
        {"Accept", "application/json"},
        {"x-amz-content-sha256",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"x-amz-security-token", "token"},
        {"x-amz-date", "20180102T030405Z"},
        {"authorization",
         "AWS4-HMAC-SHA256 Credential=akid/20180102/region/sts/aws4_request, "
         "SignedHeaders=accept;host;x-amz-content-sha256;x-amz-date;x-amz-security-token, "
         "Signature=b7927f7ac39f5b2cc34d3adf38228fc665ebe2780f5c3a006e1ec0c87e45b07c"}};

    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code, document = std::move(document)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!document.empty()) {
              receiver.onMetadataSuccess(std::move(document));
            } else {
              EXPECT_CALL(
                  *raw_metadata_fetcher_,
                  failureToString(Eq(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata)))
                  .WillRepeatedly(testing::Return("InvalidMetadata"));
              receiver.onMetadataError(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata);
            }
          } else {
            EXPECT_CALL(*raw_metadata_fetcher_,
                        failureToString(Eq(MetadataFetcher::MetadataReceiver::Failure::Network)))
                .WillRepeatedly(testing::Return("Network"));
            receiver.onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network);
          }
        }));
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  MockMetadataFetcher* raw_metadata_fetcher_;
  MetadataFetcherPtr metadata_fetcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  AssumeRoleCredentialsProviderPtr provider_;
  Event::MockTimer* timer_{};
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
};

TEST_F(AssumeRoleCredentialsProviderTest, FailedFetchingDocument) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Forbidden
  expectDocument(403, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, EmptyDocument) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, MalformedDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
not json
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, EmptyJsonResponse) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, UnexpectedResponse) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "UnexpectedResponse": ""
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, NoCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": ""
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, EmptyCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": ""
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, CredentialsWithWrongFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": 1,
        "SecretAccessKey": 2,
        "SessionToken": 3
      }
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, ExpiredTokenException) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(400, std::move(R"EOF(
{
    "Error": {
        "Code": "ExpiredTokenException",
        "Message": "Token expired: current date/time 1740387458 must be before the expiration date/time 1740319004",
        "Type": "Sender"
    },
    "RequestId": "989dcb5c-a58e-492b-92eb-d9b8c836d254"
}
)EOF"));

  // No need to restart timer since credentials are fetched from cache.
  // Even though as per `Expiration` field (in wrong format) the credentials are expired
  // the credentials won't be refreshed until the next refresh period (1hr) or new expiration
  // value implicitly set to a value same as refresh interval.

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // bad expiration format will cause a refresh of 1 hour - 60s grace period (3540 seconds) by
  // default
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3540)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, BadExpirationFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T03:04:05Z in unix_timestamp is 1514862245
  // STS API call with "Accept: application/json" is expected to return Exception in `Integer` unix
  // timestamp format. However, if non integer is returned for Expiration field, then the value will
  // be ignored and instead the expiration is set to 1 hour in future.
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": "2018-01-02T03:04:05Z"
      }
    }
  }
}
)EOF"));

  // No need to restart timer since credentials are fetched from cache.
  // Even though as per `Expiration` field (in wrong format) the credentials are expired
  // the credentials won't be refreshed until the next refresh period (1hr) or new expiration
  // value implicitly set to a value same as refresh interval.

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // bad expiration format will cause a refresh of 1 hour - 60s grace period (3540 seconds) by
  // default
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3540)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(AssumeRoleCredentialsProviderTest, FullCachedCredentialsWithMissingExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // STS API call with "Accept: application/json" is expected to return Exception in `Integer` unix
  // timestamp format. However, if Expiration field is empty, then the expiration will set to 1 hour
  // in future.
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token"
      }
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // No expiration should fall back to a one hour - 60s grace period (3540s) refresh
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3540)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(AssumeRoleCredentialsProviderTest, RefreshOnNormalCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T05:04:05Z in unix_timestamp is 1.514869445E9
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1.514869445E9
      }
    }
  }
}
)EOF"));
  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // 2 hours - 60s grace period = 7140 seconds
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(7140000), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(AssumeRoleCredentialsProviderTest, RefreshOnNormalCredentialExpirationIntegerFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T05:04:05Z in unix_timestamp is 1514869445
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1514869445
      }
    }
  }
}
)EOF"));
  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // 2 hours - 60s grace period = 7140 seconds
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(7140000), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(AssumeRoleCredentialsProviderTest, FailedFetchingDocumentDuringStartup) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Forbidden
  expectDocument(403, std::move(std::string()));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(2));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, UnexpectedResponseDuringStartup) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "UnexpectedResponse": ""
  }
}
)EOF"));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(2));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, Coverage) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "UnexpectedResponse": ""
  }
}
)EOF"));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(2));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
  ASSERT_EQ(provider_->providerName(), "AssumeRoleCredentialsProvider");
}

TEST_F(AssumeRoleCredentialsProviderTest, WithSessionDuration) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  // Custom matcher for request with session duration parameter.
  Http::TestRequestHeaderMapImpl headers_with_duration{
      {":path", "/?Version=2011-06-15&Action=AssumeRole&RoleArn=aws:iam::123456789012:role/"
                "arn&RoleSessionName=role-session-name&DurationSeconds=3600"},
      {":authority", "sts.region.amazonaws.com"},
      {":scheme", "https"},
      {":method", "GET"},
      {"Accept", "application/json"},
      {"x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"x-amz-security-token", "token"},
      {"x-amz-date", "20180102T030405Z"},
      {"authorization",
       "AWS4-HMAC-SHA256 Credential=akid/20180102/region/sts/aws4_request, "
       "SignedHeaders=accept;host;x-amz-content-sha256;x-amz-date;x-amz-security-token, "
       "Signature=88533b93b82077848fa88b5d1fe69540a0916960fdd48a1df66b764dc73a6d9a"}};

  // Use a custom expectation for this test to verify the DurationSeconds parameter.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers_with_duration), _, _))
      .WillRepeatedly(Invoke(
          [](Http::RequestMessage&, Tracing::Span&, MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));
          }));

  // Setup provider with session duration
  ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider cred_provider = {};
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");
  cred_provider.mutable_session_duration()->set_seconds(3600);

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
      .WillRepeatedly(Return("sts.region.amazonaws.com:443"));

  auto cluster_name = "credentials_provider_cluster";
  envoy::extensions::common::aws::v3::AwsCredentialProvider defaults;
  envoy::extensions::common::aws::v3::EnvironmentCredentialProvider env_provider;
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);

  defaults.mutable_environment_credential_provider()->CopyFrom(env_provider);
  auto credentials_provider_chain =
      std::make_shared<Extensions::Common::Aws::CommonCredentialsProviderChain>(context_, "region",
                                                                                defaults);
  auto signer = std::make_unique<SigV4SignerImpl>(
      STS_SERVICE_NAME, "region", credentials_provider_chain, context_,
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

  provider_ = std::make_shared<AssumeRoleCredentialsProvider>(
      context_, mock_manager_, cluster_name,
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      "region", MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
      std::move(signer), cred_provider);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
  EXPECT_EQ("test-access-key", credentials.accessKeyId().value());
}

TEST_F(AssumeRoleCredentialsProviderTest, TimerDisableAndFetcherCancel) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));

  setupProvider();

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, AsyncCallbackSetup) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, CredentialsPendingFlag) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, MetadataFetcherCreateAndCancel) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));

  setupProvider();

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
}

TEST_F(AssumeRoleCredentialsProviderTest, CredentialsPendingReturn) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider cred_provider = {};
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
      .WillRepeatedly(Return("sts.region.amazonaws.com:443"));

  auto cluster_name = "credentials_provider_cluster";
  auto credentials_provider_chain = std::make_shared<MockCredentialsProviderChain>();

  // Set up mock chain to return true (credentials pending)
  EXPECT_CALL(*credentials_provider_chain, addCallbackIfChainCredentialsPending(_))
      .WillRepeatedly(Return(true));

  auto signer = std::make_unique<SigV4SignerImpl>(
      STS_SERVICE_NAME, "region", credentials_provider_chain, context_,
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

  provider_ = std::make_shared<AssumeRoleCredentialsProvider>(
      context_, mock_manager_, cluster_name,
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      "region", MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
      std::move(signer), cred_provider);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  delete (raw_metadata_fetcher_);
}

TEST_F(AssumeRoleCredentialsProviderTest, WithExternalId) {
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  // Custom matcher for request with external ID parameter.
  Http::TestRequestHeaderMapImpl headers_with_external_id{
      {":path", "/?Version=2011-06-15&Action=AssumeRole&RoleArn=aws:iam::123456789012:role/"
                "arn&RoleSessionName=role-session-name&ExternalId=test-external-id"},
      {":authority", "sts.region.amazonaws.com"},
      {":scheme", "https"},
      {":method", "GET"},
      {"Accept", "application/json"},
      {"x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"x-amz-security-token", "token"},
      {"x-amz-date", "20180102T030405Z"},
      {"authorization",
       "AWS4-HMAC-SHA256 Credential=akid/20180102/region/sts/aws4_request, "
       "SignedHeaders=accept;host;x-amz-content-sha256;x-amz-date;x-amz-security-token, "
       "Signature=cc05851f97c3e6c1d8c28c205a1dcbb6248a944375f3e7b349800c2b3744fc48"}};

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers_with_external_id), _, _))
      .WillRepeatedly(Invoke(
          [](Http::RequestMessage&, Tracing::Span&, MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(R"EOF(
{
  "AssumeRoleResponse": {
    "AssumeRoleResult": {
      "Credentials": {
        "AccessKeyId": "test-access-key",
        "SecretAccessKey": "test-secret-key",
        "SessionToken": "test-session-token"
      }
    }
  }
}
)EOF"));
          }));

  // Setup provider with external ID.
  ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider cred_provider = {};
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");
  cred_provider.set_external_id("test-external-id");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
      .WillRepeatedly(Return("sts.region.amazonaws.com:443"));

  auto cluster_name = "credentials_provider_cluster";
  envoy::extensions::common::aws::v3::AwsCredentialProvider defaults;
  envoy::extensions::common::aws::v3::EnvironmentCredentialProvider env_provider;
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);

  defaults.mutable_environment_credential_provider()->CopyFrom(env_provider);
  auto credentials_provider_chain =
      std::make_shared<Extensions::Common::Aws::CommonCredentialsProviderChain>(context_, "region",
                                                                                defaults);
  auto signer = std::make_unique<SigV4SignerImpl>(
      STS_SERVICE_NAME, "region", credentials_provider_chain, context_,
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{},
      Extensions::Common::Aws::AwsSigningHeaderMatcherVector{});

  provider_ = std::make_shared<AssumeRoleCredentialsProvider>(
      context_, mock_manager_, cluster_name,
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      "region", MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
      std::move(signer), cred_provider);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_TRUE(credentials.accessKeyId().has_value());
  EXPECT_EQ("test-access-key", credentials.accessKeyId().value());
}

// Tests ASAN failure when cancel wrapper is not used
TEST_F(AssumeRoleCredentialsProviderTest, CancelWrapperPreventsUseAfterFree) {
  std::function<void()> captured_callback;

  EXPECT_CALL(context_.thread_local_, runOnAllThreads(testing::_, testing::_))
      .WillOnce(testing::Invoke([&captured_callback](const std::function<void()>&,
                                                     const std::function<void()>& complete_cb) {
        captured_callback = complete_cb;
      }));

  setupProvider();

  {
    auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
    provider_friend.setCredentialsToAllThreads(std::make_unique<Credentials>());

    ASSERT_TRUE(captured_callback != nullptr);

    provider_friend.provider_.reset();
    provider_.reset();
  }

  captured_callback();
  delete raw_metadata_fetcher_;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
