#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/credential_providers/instance_profile_credentials_provider.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
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

// Begin unit test for new option via Http Async client.
class InstanceProfileCredentialsProviderTest : public testing::Test {
public:
  InstanceProfileCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {}

  void setupProvider(MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
                         MetadataFetcher::MetadataReceiver::RefreshState::Ready,
                     std::chrono::seconds initialization_timer = std::chrono::seconds(2)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    mock_manager_ = std::make_shared<MockAwsClusterManager>();
    base_manager_ = std::dynamic_pointer_cast<AwsClusterManager>(mock_manager_);

    manager_optref_.emplace(base_manager_);
    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("169.254.170.2:80/path/to/doc"));

    provider_ = std::make_shared<InstanceProfileCredentialsProvider>(
        *api_, context_, manager_optref_, nullptr,
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        refresh_state, initialization_timer, "credentials_provider_cluster");
  }

  void expectSessionToken(const uint64_t status_code, const std::string&& token) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/api/token"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "PUT"},
                                           {"X-aws-ec2-metadata-token-ttl-seconds", "21600"}};
    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code, token = std::move(token)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!token.empty()) {
              receiver.onMetadataSuccess(std::move(token));
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

  void expectCredentialListing(const uint64_t status_code, const std::string&& instance_role) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"}};
    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code, instance_role = std::move(instance_role)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!instance_role.empty()) {
              receiver.onMetadataSuccess(std::move(instance_role));
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

  void expectCredentialListingIMDSv2(const uint64_t status_code,
                                     const std::string&& instance_role) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"},
                                           {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code, instance_role = std::move(instance_role)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!instance_role.empty()) {
              receiver.onMetadataSuccess(std::move(instance_role));
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

  void expectDocument(const uint64_t status_code, const std::string&& credential_document_value) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":scheme", "http"},
        {":method", "GET"}};
    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code,
                                credential_document_value = std::move(credential_document_value)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!credential_document_value.empty()) {
              receiver.onMetadataSuccess(std::move(credential_document_value));
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

  void expectDocumentIMDSv2(const uint64_t status_code,
                            const std::string&& credential_document_value) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":scheme", "http"},
        {":method", "GET"},
        {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
        .WillRepeatedly(Invoke([this, status_code,
                                credential_document_value = std::move(credential_document_value)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK)) {
            if (!credential_document_value.empty()) {
              receiver.onMetadataSuccess(std::move(credential_document_value));
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
  InstanceProfileCredentialsProviderPtr provider_;
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks_{};
  Event::MockTimer* timer_{};
  std::chrono::milliseconds expected_duration_;
  OptRef<std::shared_ptr<AwsClusterManager>> manager_optref_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  std::shared_ptr<AwsClusterManager> base_manager_;
};

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(403 /*Forbidden*/, std::move(std::string()));
  expectCredentialListing(403 /*Forbidden*/, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  // Unauthorized
  expectCredentialListingIMDSv2(401, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListingIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("")));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListingIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("")));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyListCredentialListingIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("\n")));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyListCredentialListingIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("\n")));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, FailedDocumentIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  // Unauthorized
  expectDocument(401, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, FailedDocumentIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  // Unauthorized
  expectDocumentIMDSv2(401, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocumentIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocument(200, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocumentIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocumentIMDSv2(200, std::move(std::string()));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumentIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1")));
  expectDocument(200, std::move(R"EOF(
 not json
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumentIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1")));
  expectDocumentIMDSv2(200, std::move(R"EOF(
 not json
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValuesIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1")));
  expectDocument(200, std::move(R"EOF(
 {
   "AccessKeyId": "",
   "SecretAccessKey": "",
   "Token": ""
 }
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValuesIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1")));
  expectDocumentIMDSv2(200, std::move(R"EOF(
 {
   "AccessKeyId": "",
   "SecretAccessKey": "",
   "Token": ""
 }
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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

TEST_F(InstanceProfileCredentialsProviderTest, RefreshOnCredentialExpirationIMDSv1) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1")));
  expectDocument(200, std::move(R"EOF(
 {
   "AccessKeyId": "akid",
   "SecretAccessKey": "secret",
   "Token": "token"
 }
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, RefreshOnCredentialExpirationIMDSv2) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1")));
  expectDocumentIMDSv2(200, std::move(R"EOF(
 {
   "AccessKeyId": "akid",
   "SecretAccessKey": "secret",
   "Token": "token"
 }
 )EOF"));

  setupProvider();
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(
                                       MetadataCredentialsProviderBase::getCacheDuration()),
                                   nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingIMDSv2(200, std::move(std::string("doc1")));
  expectDocumentIMDSv2(200, std::move(R"EOF(
 {
   "AccessKeyId": "new_akid",
   "SecretAccessKey": "new_secret",
   "Token": "new_token1"
 }
 )EOF"));
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingIMDSv1DuringStartup) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(403 /*Forbidden*/, std::move(std::string()));
  expectCredentialListing(403 /*Forbidden*/, std::move(std::string()));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(2));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingIMDSv2DuringStartup) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  // Unauthorized
  expectCredentialListingIMDSv2(401, std::move(std::string()));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(2));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
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

TEST_F(InstanceProfileCredentialsProviderTest,
       FailedCredentialListingIMDSv1DuringStartupMaxRetries30s) {
  // Setup timer.

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(403 /*Forbidden*/, std::move(std::string()));
  expectCredentialListing(403 /*Forbidden*/, std::move(std::string()));

  setupProvider(MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh,
                std::chrono::seconds(16));
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(16)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(32)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();

  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // We max out at 32 seconds
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(32)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

// End unit test for new option via Http Async client.

// Begin unit test for deprecated option using Libcurl client.
// TODO(suniltheta): Remove this test class once libcurl is removed from Envoy.
class InstanceProfileCredentialsProviderUsingLibcurlTest : public testing::Test {
public:
  InstanceProfileCredentialsProviderUsingLibcurlTest()
      : api_(Api::createApiForTest(time_system_)) {}

  void setupProvider() {

    provider_ = std::make_shared<InstanceProfileCredentialsProvider>(
        *api_, absl::nullopt, absl::nullopt,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        nullptr, MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
        "credentials_provider_cluster");
  }

  void expectSessionToken(const absl::optional<std::string>& token) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/api/token"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "PUT"},
                                           {"X-aws-ec2-metadata-token-ttl-seconds", "21600"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(token));
  }

  void expectCredentialListing(const absl::optional<std::string>& listing) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(listing));
  }

  void expectCredentialListingIMDSv2(const absl::optional<std::string>& listing) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"},
                                           {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(listing));
  }

  void expectDocument(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":scheme", "http"},
        {":method", "GET"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  void expectDocumentIMDSv2(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":scheme", "http"},
        {":method", "GET"},
        {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  TestScopedRuntime scoped_runtime_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  InstanceProfileCredentialsProviderPtr provider_;
};

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FailedCredentialListingIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FailedCredentialListingIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyCredentialListingIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyCredentialListingIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("\n");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyListCredentialListingIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("\n");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyListCredentialListingIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MissingDocumentIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1\ndoc2\ndoc3");
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MissingDocumentIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1\ndoc2\ndoc3");
  expectDocumentIMDSv2(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MalformedDocumentIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
not json
 )EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MalformedDocumentIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1");
  expectDocumentIMDSv2(R"EOF(
not json
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyValuesIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": ""
}
 )EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyValuesIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1");
  expectDocumentIMDSv2(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": ""
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FullCachedCredentialsIMDSv1) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
 )EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FullCachedCredentialsIMDSv2) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1");
  expectDocumentIMDSv2(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, CredentialExpirationIMDSv1) {
  setupProvider();
  InSequence sequence;
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
 )EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.advanceTimeWait(std::chrono::hours(2));
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token"
}
 )EOF");
  const auto new_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, CredentialExpirationIMDSv2) {
  setupProvider();
  InSequence sequence;
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1");
  expectDocumentIMDSv2(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.advanceTimeWait(std::chrono::hours(2));
  expectSessionToken("TOKEN");
  expectCredentialListingIMDSv2("doc1");
  expectDocumentIMDSv2(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token"
}
)EOF");
  const auto new_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}
// End unit test for deprecated option using Libcurl client.

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
