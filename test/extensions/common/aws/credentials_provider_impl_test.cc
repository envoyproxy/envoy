#include <cstddef>
#include <string>

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

using Envoy::Extensions::Common::Aws::MetadataFetcher;
using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using Envoy::Extensions::Common::Aws::MockMetadataFetcher;
using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

const char CREDENTIALS_FILE[] = "test-credentials.json";
const char CREDENTIALS_FILE_CONTENTS[] =
    R"(
[default]
aws_access_key_id=default_access_key
aws_secret_access_key=default_secret
aws_session_token=default_token

# This profile has leading spaces that should get trimmed.
  [profile1]
# The "=" in the value should not interfere with how this line is parsed.
aws_access_key_id=profile1_acc=ess_key
aws_secret_access_key=profile1_secret
foo=bar
aws_session_token=profile1_token

[profile2]
aws_access_key_id=profile2_access_key

[profile3]
aws_access_key_id=profile3_access_key
aws_secret_access_key=

[profile4]
aws_access_key_id = profile4_access_key
aws_secret_access_key = profile4_secret
aws_session_token = profile4_token
)";

MATCHER_P(WithName, expectedName, "") {
  *result_listener << "\nexpected { name: \"" << expectedName << "\"} but got {name: \""
                   << arg.name() << "\"}\n";
  return ExplainMatchResult(expectedName, arg.name(), result_listener);
}

MATCHER_P(WithAttribute, expectedCluster, "") {
  const auto argSocketAddress =
      arg.load_assignment().endpoints()[0].lb_endpoints()[0].endpoint().address().socket_address();
  const auto expectedSocketAddress = expectedCluster.load_assignment()
                                         .endpoints()[0]
                                         .lb_endpoints()[0]
                                         .endpoint()
                                         .address()
                                         .socket_address();

  *result_listener << "\nexpected {cluster name: \"" << expectedCluster.name() << "\", type: \""
                   << expectedCluster.type() << "\", socket address: \""
                   << expectedSocketAddress.address() << "\", port: \""
                   << expectedSocketAddress.port_value() << "\", transport socket enabled: \""
                   << expectedCluster.has_transport_socket() << "\"},\n but got {cluster name: \""
                   << arg.name() << "\", type: \"" << arg.type() << "\", socket address: \""
                   << argSocketAddress.address() << "\", port: \"" << argSocketAddress.port_value()
                   << "\", transport socket enabled: \"" << arg.has_transport_socket() << "\"}\n";
  return ExplainMatchResult(expectedCluster.name(), arg.name(), result_listener) &&
         ExplainMatchResult(expectedCluster.type(), arg.type(), result_listener) &&
         ExplainMatchResult(expectedSocketAddress.address(), argSocketAddress.address(),
                            result_listener) &&
         ExplainMatchResult(expectedSocketAddress.port_value(), argSocketAddress.port_value(),
                            result_listener) &&
         ExplainMatchResult(expectedCluster.has_transport_socket(), arg.has_transport_socket(),
                            result_listener);
}

class EvironmentCredentialsProviderTest : public testing::Test {
public:
  ~EvironmentCredentialsProviderTest() override {
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
    TestEnvironment::unsetEnvVar("AWS_SESSION_TOKEN");
  }

  EnvironmentCredentialsProvider provider_;
};

TEST_F(EvironmentCredentialsProviderTest, AllEnvironmentVars) {
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(EvironmentCredentialsProviderTest, NoEnvironmentVars) {
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(EvironmentCredentialsProviderTest, MissingAccessKeyId) {
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(EvironmentCredentialsProviderTest, NoSessionToken) {
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

class CredentialsFileCredentialsProviderTest : public testing::Test {
public:
  CredentialsFileCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), provider_(*api_) {}

  ~CredentialsFileCredentialsProviderTest() override {
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
  }

  void setUpTest(std::string file_contents, std::string profile) {
    auto file_path = TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, file_contents);
    TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);
    TestEnvironment::setEnvVar("AWS_PROFILE", profile, 1);
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  CredentialsFileCredentialsProvider provider_;
};

TEST_F(CredentialsFileCredentialsProviderTest, FileDoesNotExist) {
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", "/file/does/not/exist", 1);

  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, ProfileDoesNotExist) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "invalid_profile");

  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, IncompleteProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile2");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile2_access_key", credentials.accessKeyId().value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, DefaultProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, CompleteProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile1");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, EmptySecret) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile3");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile3_access_key", credentials.accessKeyId().value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, SpacesBetweenParts) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile4");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile4_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile4_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile4_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, RefreshInterval) {
  InSequence sequence;
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", "/file/does/not/exist", 1);

  auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());

  // Credentials won't be extracted even after we switch to a legitimate profile
  // with valid credentials.
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile1");
  credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());

  // Credentials will be extracted again after the REFRESH_INTERVAL.
  time_system_.advanceTimeWait(std::chrono::hours(2));
  credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());

  // Previously cached credentials will be used.
  setUpTest(CREDENTIALS_FILE_CONTENTS, "default");
  credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());

  // Credentials will be extracted again after the REFRESH_INTERVAL.
  time_system_.advanceTimeWait(std::chrono::hours(2));
  credentials = provider_.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

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

  void setupProvider() {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.use_http_client_to_fetch_aws_credentials", "true"}});
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<InstanceProfileCredentialsProvider>(
        *api_, context_,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        "credentials_provider_cluster");
  }

  void setupProviderWithContext() {
    EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));

    setupProvider();
    expected_duration_ = provider_->getCacheDuration();
    init_target_handle_->initialize(init_watcher_);
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

  void expectCredentialListingSecure(const uint64_t status_code,
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

  void expectDocumentSecure(const uint64_t status_code,
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
  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Event::MockTimer* timer_{};
  std::chrono::milliseconds expected_duration_;
};

TEST_F(InstanceProfileCredentialsProviderTest, TestAddMissingCluster) {
  // Setup without thread local cluster yet
  envoy::config::cluster::v3::Cluster expected_cluster;
  constexpr static const char* kStaticCluster = R"EOF(
name: credentials_provider_cluster
type: static
connectTimeout: 2s
lb_policy: ROUND_ROBIN
loadAssignment:
  clusterName: credentials_provider_cluster
  endpoints:
  - lbEndpoints:
    - endpoint:
        address:
          socketAddress:
            address: "169.254.169.254"
            portValue: 80
typed_extension_protocol_options:
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    explicit_http_config:
      http_protocol_options:
        accept_http_10: true
 )EOF";
  MessageUtil::loadFromYaml(kStaticCluster, expected_cluster,
                            ProtobufMessage::getNullValidationVisitor());

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithAttribute(expected_cluster), _))
      .WillOnce(Return(true));

  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  expectDocumentSecure(200, std::move(R"EOF(
 {
   "AccessKeyId": "akid",
   "SecretAccessKey": "secret",
   "Token": "token"
 }
 )EOF"));

  setupProviderWithContext();
}

TEST_F(InstanceProfileCredentialsProviderTest, TestClusterMissing) {
  // Setup without thread local cluster
  Http::RequestMessageImpl message;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithName("credentials_provider_cluster"), _))
      .WillOnce(Throw(EnvoyException("exeption message")));

  // init_watcher ready is not called.
  init_watcher_.expectReady().Times(0);
  setupProvider();
  // Below line is not testing anything, will just avoid asan failure with memory leak.
  metadata_fetcher_.reset(raw_metadata_fetcher_);
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(403 /*Forbidden*/, std::move(std::string()));
  expectCredentialListing(403 /*Forbidden*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(401 /*Unauthorized*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListingUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("")));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called once for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListingSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("")));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyListCredentialListingUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("\n")));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called once for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyListCredentialListingSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("\n")));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedDocumentUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocument(401 /*Unauthorized*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedDocumentSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocumentSecure(401 /*Unauthorized*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocumentUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocument(200, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocumentSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1\ndoc2\ndoc3")));
  expectDocumentSecure(200, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumentUnsecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1")));
  expectDocument(200, std::move(R"EOF(
 not json
 )EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumentSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  expectDocumentSecure(200, std::move(R"EOF(
 not json
 )EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is called thrice
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValuesUnsecure) {
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValuesSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  expectDocumentSecure(200, std::move(R"EOF(
 {
   "AccessKeyId": "",
   "SecretAccessKey": "",
   "Token": ""
 }
 )EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FullCachedCredentialsUnsecure) {
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FullCachedCredentialsSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  expectDocumentSecure(200, std::move(R"EOF(
 {
   "AccessKeyId": "akid",
   "SecretAccessKey": "secret",
   "Token": "token"
 }
 )EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started after fetch done from init.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, RefreshOnCredentialExpirationUnsecure) {
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  expectSessionToken(200, std::move(std::string()));
  expectCredentialListing(200, std::move(std::string("doc1")));
  expectDocument(200, std::move(R"EOF(
 {
   "AccessKeyId": "new_akid",
   "SecretAccessKey": "new_secret",
   "Token": "new_token1"
 }
 )EOF"));

  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called thrice back to back to back.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto new_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token1", new_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, RefreshOnCredentialExpirationSecure) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  expectDocumentSecure(200, std::move(R"EOF(
 {
   "AccessKeyId": "akid",
   "SecretAccessKey": "secret",
   "Token": "token"
 }
 )EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Cancel is called twice.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Cancel is not called again as we don't expect any more call to fetch until timeout.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  expectSessionToken(200, std::move("TOKEN"));
  expectCredentialListingSecure(200, std::move(std::string("doc1")));
  expectDocumentSecure(200, std::move(R"EOF(
 {
   "AccessKeyId": "new_akid",
   "SecretAccessKey": "new_secret",
   "Token": "new_token1"
 }
 )EOF"));

  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called thrice back to back to back.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(3);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto new_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token1", new_credentials.sessionToken().value());
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
        *api_, absl::nullopt,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        nullptr, "credentials_provider_cluster");
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

  void expectCredentialListingSecure(const absl::optional<std::string>& listing) {
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

  void expectDocumentSecure(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":scheme", "http"},
        {":method", "GET"},
        {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  InstanceProfileCredentialsProviderPtr provider_;
};

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FailedCredentialListingUnsecure) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FailedCredentialListingSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyCredentialListingUnsecure) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyCredentialListingSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("\n");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyListCredentialListingUnsecure) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("\n");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyListCredentialListingSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MissingDocumentUnsecure) {
  setupProvider();
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1\ndoc2\ndoc3");
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MissingDocumentSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1\ndoc2\ndoc3");
  expectDocumentSecure(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MalformedDocumentUnsecure) {
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, MalformedDocumentSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
not json
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyValuesUnsecure) {
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, EmptyValuesSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FullCachedCredentialsUnsecure) {
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, FullCachedCredentialsSecure) {
  setupProvider();
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, CredentialExpirationUnsecure) {
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

TEST_F(InstanceProfileCredentialsProviderUsingLibcurlTest, CredentialExpirationSecure) {
  setupProvider();
  InSequence sequence;
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
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
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
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

// Begin unit test for new option via Http Async client.
class TaskRoleCredentialsProviderTest : public testing::Test {
public:
  TaskRoleCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void setupProvider() {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.use_http_client_to_fetch_aws_credentials", "true"}});
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<TaskRoleCredentialsProvider>(
        *api_, context_,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        "169.254.170.2:80/path/to/doc", "auth_token", "credentials_provider_cluster");
  }

  void setupProviderWithContext() {
    EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    setupProvider();
    expected_duration_ = provider_->getCacheDuration();
    init_target_handle_->initialize(init_watcher_);
  }

  void expectDocument(const uint64_t status_code, const std::string&& document) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/path/to/doc"},
                                           {":authority", "169.254.170.2:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"},
                                           {"authorization", "auth_token"}};
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
  TaskRoleCredentialsProviderPtr provider_;
  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Event::MockTimer* timer_{};
  std::chrono::milliseconds expected_duration_;
};

TEST_F(TaskRoleCredentialsProviderTest, TestAddMissingCluster) {
  // Setup without thread local cluster yet
  envoy::config::cluster::v3::Cluster expected_cluster;
  constexpr static const char* kStaticCluster = R"EOF(
name: credentials_provider_cluster
type: static
connectTimeout: 2s
lb_policy: ROUND_ROBIN
loadAssignment:
  clusterName: credentials_provider_cluster
  endpoints:
  - lbEndpoints:
    - endpoint:
        address:
          socketAddress:
            address: "169.254.170.2"
            portValue: 80
typed_extension_protocol_options:
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    explicit_http_config:
      http_protocol_options:
        accept_http_10: true
 )EOF";
  MessageUtil::loadFromYaml(kStaticCluster, expected_cluster,
                            ProtobufMessage::getNullValidationVisitor());

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithAttribute(expected_cluster), _))
      .WillOnce(Return(true));

  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:05:00Z"
}
)EOF"));

  setupProviderWithContext();
}

TEST_F(TaskRoleCredentialsProviderTest, TestClusterMissing) {
  // Setup without thread local cluster
  Http::RequestMessageImpl message;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithName("credentials_provider_cluster"), _))
      .WillOnce(Throw(EnvoyException("exeption message")));
  // init_watcher ready is not called.
  init_watcher_.expectReady().Times(0);
  setupProvider();
  // Below line is not testing anything, will just avoid asan failure with memory leak.
  metadata_fetcher_.reset(raw_metadata_fetcher_);
}

TEST_F(TaskRoleCredentialsProviderTest, FailedFetchingDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(403 /*Forbidden*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, EmptyDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, MalformedDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
not json
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, EmptyValues) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": "",
  "Expiration": ""
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success with updating
  // expiration time.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, FullCachedCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:05:00Z"
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, RefreshOnNormalCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF"));
  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called once more.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, TimestampCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:04:05Z"
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // Need to disable and restart timer since credentials are expired and fetched again
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  // We call cancel once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  expectDocument(200, std::move(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF"));
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}
// End unit test for new option via Http Async client.

// Begin unit test for deprecated option using Libcurl client.
// TODO(suniltheta): Remove this test class once libcurl is removed from Envoy.
class TaskRoleCredentialsProviderUsingLibcurlTest : public testing::Test {
public:
  TaskRoleCredentialsProviderUsingLibcurlTest() : api_(Api::createApiForTest(time_system_)) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void setupProvider() {
    provider_ = std::make_shared<TaskRoleCredentialsProvider>(
        *api_, absl::nullopt,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        nullptr, "169.254.170.2:80/path/to/doc", "auth_token", "credentials_provider_cluster");
  }

  void expectDocument(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/path/to/doc"},
                                           {":authority", "169.254.170.2:80"},
                                           {":scheme", "http"},
                                           {":method", "GET"},
                                           {"authorization", "auth_token"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  TaskRoleCredentialsProviderPtr provider_;
};

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, FailedFetchingDocument) {
  setupProvider();
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, EmptyDocument) {
  setupProvider();
  expectDocument("");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, MalformedDocument) {
  setupProvider();
  expectDocument(R"EOF(
not json
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, EmptyValues) {
  setupProvider();
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": "",
  "Expiration": ""
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, FullCachedCredentials) {
  setupProvider();
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:05:00Z"
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

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, NormalCredentialExpiration) {
  setupProvider();
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.advanceTimeWait(std::chrono::hours(2));
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF");
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderUsingLibcurlTest, TimestampCredentialExpiration) {
  setupProvider();
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:04:05Z"
}
)EOF");
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF");
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}
// End unit test for deprecated option using Libcurl client.

class WebIdentityCredentialsProviderTest : public testing::Test {
public:
  WebIdentityCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void setupProvider() {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.use_http_client_to_fetch_aws_credentials", "true"}});
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<WebIdentityCredentialsProvider>(
        *api_, context_,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        TestEnvironment::writeStringToFileForTest("web_token_file", "web_token"),
        "sts.region.amazonaws.com:443", "aws:iam::123456789012:role/arn", "role-session-name",
        "credentials_provider_cluster");
  }

  void setupProviderWithContext() {
    EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    setupProvider();
    expected_duration_ = provider_->getCacheDuration();
    init_target_handle_->initialize(init_watcher_);
  }

  void setupProviderWithLibcurl() {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    provider_ = std::make_shared<WebIdentityCredentialsProvider>(
        *api_, context_,
        [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        },
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        TestEnvironment::writeStringToFileForTest("web_token_file", "web_token"),
        "sts.region.amazonaws.com:443", "aws:iam::123456789012:role/arn", "role-session-name",
        "credentials_provider_cluster");
  }

  void expectDocument(const uint64_t status_code, const std::string&& document) {
    Http::TestRequestHeaderMapImpl headers{{":path",
                                            "/?Action=AssumeRoleWithWebIdentity"
                                            "&Version=2011-06-15&RoleSessionName=role-session-name"
                                            "&RoleArn=aws:iam::123456789012:role/arn"
                                            "&WebIdentityToken=web_token"},
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
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Event::MockTimer* timer_{};
  std::chrono::milliseconds expected_duration_;
};

TEST_F(WebIdentityCredentialsProviderTest, TestAddMissingCluster) {
  // Setup without thread local cluster yet
  envoy::config::cluster::v3::Cluster expected_cluster;
  constexpr static const char* kStaticCluster = R"EOF(
name: credentials_provider_cluster
type: logical_dns
connectTimeout: 2s
lb_policy: ROUND_ROBIN
loadAssignment:
  clusterName: credentials_provider_cluster
  endpoints:
  - lbEndpoints:
    - endpoint:
        address:
          socketAddress:
            address: "sts.region.amazonaws.com"
            portValue: 443
typed_extension_protocol_options:
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    explicit_http_config:
      http_protocol_options:
        accept_http_10: true
transport_socket:
  name: envoy.transport_sockets.tls
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
 )EOF";
  MessageUtil::loadFromYaml(kStaticCluster, expected_cluster,
                            ProtobufMessage::getNullValidationVisitor());

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithAttribute(expected_cluster), _))
      .WillOnce(Return(true));

  // Time 2018-01-02T03:05:00Z in unix_timestamp is 1514862300
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1514862300
      }
    }
  }
}
)EOF"));
  setupProviderWithContext();
}

TEST_F(WebIdentityCredentialsProviderTest, TestClusterMissing) {
  // Setup without thread local cluster
  Http::RequestMessageImpl message;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(cluster_manager_, addOrUpdateCluster(WithName("credentials_provider_cluster"), _))
      .WillOnce(Throw(EnvoyException("exeption message")));
  // init_watcher ready is not called.
  init_watcher_.expectReady().Times(0);
  setupProvider();
  // Below line is not testing anything, will just avoid asan failure with memory leak.
  metadata_fetcher_.reset(raw_metadata_fetcher_);
}

TEST_F(WebIdentityCredentialsProviderTest, FailedFetchingDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(403 /*Forbidden*/, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, EmptyDocument) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectDocument(200, std::move(std::string()));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success with updating
  // expiration time.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success with updating
  // expiration time.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success with updating
  // expiration time.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, FullCachedCredentials) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T03:05:00Z in unix_timestamp is 1514862300
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1514862300
      }
    }
  }
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // Cancel is called for fetching once again as previous attempt wasn't a success with updating
  // expiration time.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(WebIdentityCredentialsProviderTest, FullCachedCredentialsBadExpirationFormat) {
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  // Even though as per `Expiration` field (in wrong format) the credentials are expired
  // the credentials won't be refreshed until the next refresh period (1hr) or new expiration
  // value implicitly set to a value same as refresh interval.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // Time 2019-01-02T03:04:05Z in unix_timestamp is 1546398245
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "new_akid",
        "SecretAccessKey": "new_secret",
        "SessionToken": "new_token",
        "Expiration": "2019-01-02T03:04:05Z"
      }
    }
  }
}
)EOF"));
  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called once more.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
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
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  // The credentials won't be refreshed until the next refresh period (1hr) or new expiration
  // value implicitly set to a value same as refresh interval.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // Time 2019-01-02T03:04:05Z in unix_timestamp is 1546398245
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "new_akid",
        "SecretAccessKey": "new_secret",
        "SessionToken": "new_token"
      }
    }
  }
}
)EOF"));
  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called once more.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, RefreshOnNormalCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T04:04:05Z in unix_timestamp is 1514865845
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1514865845
      }
    }
  }
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // No need to restart timer since credentials are fetched from cache.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // We don't expect any more call to cancel or fetch again.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  // Time 2019-01-02T03:05:00Z in unix_timestamp is 1546398300
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "new_akid",
        "SecretAccessKey": "new_secret",
        "SessionToken": "new_token",
        "Expiration": 1546398300
      }
    }
  }
}
)EOF"));
  // Expect timer to have expired but we would re-start the timer eventually after refresh.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  // Cancel will be called once more.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  time_system_.advanceTimeWait(std::chrono::minutes(61));
  timer_->invokeCallback();

  // We don't expect timer to be reset again for new fetch.
  EXPECT_CALL(*timer_, disableTimer()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr)).Times(0);
  // Similarly we won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, TimestampCredentialExpiration) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  // Time 2018-01-02T03:04:05Z in unix_timestamp is 1514862245
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "akid",
        "SecretAccessKey": "secret",
        "SessionToken": "token",
        "Expiration": 1514862245
      }
    }
  }
}
)EOF"));
  // init_watcher ready is called.
  init_watcher_.expectReady();
  // Expect refresh timer to be started.
  EXPECT_CALL(*timer_, enableTimer(_, nullptr));
  setupProviderWithContext();

  // init_watcher ready is not called again.
  init_watcher_.expectReady().Times(0);
  // Need to disable and restart timer since credentials are expired and fetched again
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  // We call cancel once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());

  // Cancel is called once.
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  // Time 2019-01-02T03:04:05Z in unix_timestamp is 1546398245
  expectDocument(200, std::move(R"EOF(
{
  "AssumeRoleWithWebIdentityResponse": {
    "AssumeRoleWithWebIdentityResult": {
      "Credentials": {
        "AccessKeyId": "new_akid",
        "SecretAccessKey": "new_secret",
        "SessionToken": "new_token",
        "Expiration": 1546398245
      }
    }
  }
}
)EOF"));
  // Expect refresh timer to be stopped and started.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*timer_, enableTimer(expected_duration_, nullptr));
  const auto cached_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(WebIdentityCredentialsProviderTest, LibcurlEnabled) {
  setupProviderWithLibcurl();
  // Won't call fetch or cancel on metadata fetcher.
  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).Times(0);
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(0);

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());

  // Below line is not testing anything, will just avoid asan failure with memory leak.
  metadata_fetcher_.reset(raw_metadata_fetcher_);
}

class DefaultCredentialsProviderChainTest : public testing::Test {
public:
  DefaultCredentialsProviderChainTest() : api_(Api::createApiForTest(time_system_)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    cluster_manager_.initializeThreadLocalClusters({"credentials_provider_cluster"});
    EXPECT_CALL(factories_, createEnvironmentCredentialsProvider());
  }

  ~DefaultCredentialsProviderChainTest() override {
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_EC2_METADATA_DISABLED");
    TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
    TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
  }

  class MockCredentialsProviderChainFactories : public CredentialsProviderChainFactories {
  public:
    MOCK_METHOD(CredentialsProviderSharedPtr, createEnvironmentCredentialsProvider, (), (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createCredentialsFileCredentialsProvider, (Api::Api&),
                (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createWebIdentityCredentialsProvider,
                (Api::Api&, ServerFactoryContextOptRef,
                 const MetadataCredentialsProviderBase::CurlMetadataFetcher&,
                 CreateMetadataFetcherCb, absl::string_view, absl::string_view, absl::string_view,
                 absl::string_view, absl::string_view),
                (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createTaskRoleCredentialsProvider,
                (Api::Api&, ServerFactoryContextOptRef,
                 const MetadataCredentialsProviderBase::CurlMetadataFetcher&,
                 CreateMetadataFetcherCb, absl::string_view, absl::string_view, absl::string_view),
                (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createInstanceProfileCredentialsProvider,
                (Api::Api&, ServerFactoryContextOptRef,
                 const MetadataCredentialsProviderBase::CurlMetadataFetcher&,
                 CreateMetadataFetcherCb, absl::string_view),
                (const));
  };

  TestScopedRuntime scoped_runtime_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<MockCredentialsProviderChainFactories> factories_;
};

TEST_F(DefaultCredentialsProviderChainTest, NoEnvironmentVars) {
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));

  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, CredentialsFileDisabled) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.enable_aws_credentials_file", "false"}});
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_))).Times(0);
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _))
      .Times(0);
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataNotDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "false", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, RelativeUri) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(Ref(*api_), _, _, _, _,
                                                            "169.254.170.2:80/path/to/creds", ""));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriNoAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(Ref(*api_), _, _, _, _,
                                                            "http://host/path/to/creds", ""));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriWithAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(
                              Ref(*api_), _, _, _, _, "http://host/path/to/creds", "auth_token"));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentityRoleArn) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentitySessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567890));
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_,
              createWebIdentityCredentialsProvider(
                  Ref(*api_), _, _, _, _, "/path/to/web_token", "sts.region.amazonaws.com:443",
                  "aws:iam::123456789012:role/arn", "1234567890000000"));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));

  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithSessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _));
  EXPECT_CALL(factories_,
              createWebIdentityCredentialsProvider(
                  Ref(*api_), _, _, _, _, "/path/to/web_token", "sts.region.amazonaws.com:443",
                  "aws:iam::123456789012:role/arn", "role-session-name"));
  DefaultCredentialsProviderChain chain(*api_, context_, "region", DummyMetadataFetcher(),
                                        factories_);
}

TEST(CredentialsProviderChainTest, getCredentials_noCredentials) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  EXPECT_CALL(*mock_provider1, getCredentials());
  EXPECT_CALL(*mock_provider2, getCredentials());

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  const Credentials creds = chain.getCredentials();
  EXPECT_EQ(Credentials(), creds);
}

TEST(CredentialsProviderChainTest, getCredentials_firstProviderReturns) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  const Credentials creds("access_key", "secret_key");

  EXPECT_CALL(*mock_provider1, getCredentials()).WillOnce(Return(creds));
  EXPECT_CALL(*mock_provider2, getCredentials()).Times(0);

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  const Credentials ret_creds = chain.getCredentials();
  EXPECT_EQ(creds, ret_creds);
}

TEST(CredentialsProviderChainTest, getCredentials_secondProviderReturns) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  const Credentials creds("access_key", "secret_key");

  EXPECT_CALL(*mock_provider1, getCredentials());
  EXPECT_CALL(*mock_provider2, getCredentials()).WillOnce(Return(creds));

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  const Credentials ret_creds = chain.getCredentials();
  EXPECT_EQ(creds, ret_creds);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
