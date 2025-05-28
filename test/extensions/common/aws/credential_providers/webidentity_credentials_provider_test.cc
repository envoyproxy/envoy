#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

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

class WebIdentityCredentialsProviderTest : public testing::Test {
public:
  WebIdentityCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void setupProvider(MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
                         MetadataFetcher::MetadataReceiver::RefreshState::Ready,
                     std::chrono::seconds initialization_timer = std::chrono::seconds(2)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    std::string token_file_path;
    envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
        {};

    if (token_.empty()) {
      token_file_path = TestEnvironment::writeStringToFileForTest("web_token_file", "web_token");
      cred_provider.mutable_web_identity_token_data_source()->set_inline_string("web_token");
    } else {
      cred_provider.mutable_web_identity_token_data_source()->set_inline_string(token_);
    }
    cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
    cred_provider.set_role_session_name("role-session-name");

    mock_manager_ = std::make_shared<MockAwsClusterManager>();

    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("sts.region.amazonaws.com:443"));

    auto cluster_name = "credentials_provider_cluster";

    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<WebIdentityCredentialsProvider>(
        context_, mock_manager_, cluster_name,
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        refresh_state, initialization_timer, cred_provider);
    EXPECT_EQ(provider_->providerName(), "WebIdentityCredentialsProvider");
  }

  void expectDocument(const uint64_t status_code, const std::string&& document) {
    std::string exp_token = token_.empty() ? "web_token" : token_;
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/?Action=AssumeRoleWithWebIdentity"
                  "&Version=2011-06-15&RoleSessionName=role-session-name"
                  "&RoleArn=aws:iam::123456789012:role/arn"
                  "&WebIdentityToken=" +
                      exp_token},
        {":authority", "sts.region.amazonaws.com"},
        {":scheme", "https"},
        {":method", "GET"},
        {"Accept", "application/json"}};
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

  TestScopedRuntime scoped_runtime_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  MockMetadataFetcher* raw_metadata_fetcher_;
  MetadataFetcherPtr metadata_fetcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  WebIdentityCredentialsProviderPtr provider_;
  Init::TargetHandlePtr init_target_handle_;
  Event::MockTimer* timer_{};
  std::chrono::milliseconds expected_duration_;
  Upstream::ClusterUpdateCallbacks* cb_{};
  testing::NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<Upstream::MockThreadLocalCluster> test_cluster;
  std::string token_ = "";
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
};

TEST_F(WebIdentityCredentialsProviderTest, FailedFetchingDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Forbidden
  expectDocument(403, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, EmptyDocument) {
  // Test that the static prefetched token will be used instead of the local file.
  token_ = "prefetched_token";

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, MalformedDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
not json
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, EmptyJsonResponse) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, UnexpectedResponse) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "UnexpectedResponse": ""
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, NoCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": ""
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, EmptyCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": ""
    }
  }
}
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, CredentialsWithWrongFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
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

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, ExpiredTokenException) {
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

  // bad expiration format will cause a refresh of 1 hour - 5s (3595 seconds) by default
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3595)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, BadExpirationFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T03:04:05Z in unix_timestamp is 1514862245
  // STS API call with "Accept: application/json" is expected to return Exception in `Integer` unix
  // timestamp format. However, if non integer is returned for Expiration field, then the value will
  // be ignored and instead the expiration is set to 1 hour in future.
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
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

  // bad expiration format will cause a refresh of 1 hour - 5s (3595 seconds) by default
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3595)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, FullCachedCredentialsWithMissingExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // STS API call with "Accept: application/json" is expected to return Exception in `Integer` unix
  // timestamp format. However, if Expiration field is empty, then the expiration will set to 1 hour
  // in future.
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
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

  // No expiration should fall back to a one hour - 5s (3595s) refresh
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3595)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, RefreshOnNormalCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T05:04:05Z in unix_timestamp is 1.514869445E9
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
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

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::hours(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, RefreshOnNormalCredentialExpirationIntegerFormat) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T05:04:05Z in unix_timestamp is 1514869445
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
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

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::hours(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, FailedFetchingDocumentDuringStartup) {

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

TEST_F(WebIdentityCredentialsProviderTest, UnexpectedResponseDuringStartup) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
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

TEST_F(WebIdentityCredentialsProviderTest, TestCancel) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
not json
)EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  auto mock_fetcher = std::make_unique<MockMetadataFetcher>();

  EXPECT_CALL(*mock_fetcher, cancel);
  EXPECT_CALL(*mock_fetcher, fetch(_, _, _));
  // Ensure we have a metadata fetcher configured, so we expect this to receive a cancel
  provider_friend.setMetadataFetcher(std::move(mock_fetcher));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
  delete (raw_metadata_fetcher_);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
