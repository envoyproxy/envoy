#include "source/extensions/common/aws/credentials_provider_impl.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

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

class InstanceProfileCredentialsProviderTest : public testing::Test {
public:
  InstanceProfileCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)),
        provider_(*api_, [this](Http::RequestMessage& message) -> absl::optional<std::string> {
          return this->fetch_metadata_.fetch(message);
        }) {}

  void expectSessionToken(const absl::optional<std::string>& token) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/api/token"},
                                           {":authority", "169.254.169.254:80"},
                                           {":method", "PUT"},
                                           {"X-aws-ec2-metadata-token-ttl-seconds", "21600"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(token));
  }

  void expectCredentialListing(const absl::optional<std::string>& listing) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":method", "GET"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(listing));
  }

  void expectCredentialListingSecure(const absl::optional<std::string>& listing) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/latest/meta-data/iam/security-credentials"},
                                           {":authority", "169.254.169.254:80"},
                                           {":method", "GET"},
                                           {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(listing));
  }

  void expectDocument(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":method", "GET"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  void expectDocumentSecure(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{
        {":path", "/latest/meta-data/iam/security-credentials/doc1"},
        {":authority", "169.254.169.254:80"},
        {":method", "GET"},
        {"X-aws-ec2-metadata-token", "TOKEN"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  InstanceProfileCredentialsProvider provider_;
};

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListing) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentialListingSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListing) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListingSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocument) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1\ndoc2\ndoc3");
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocumentSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1\ndoc2\ndoc3");
  expectDocumentSecure(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumenet) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
not json
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumentSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
not json
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValues) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": ""
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValuesSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": ""
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FullCachedCredentials) {
  expectSessionToken(absl::optional<std::string>());
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FullCachedCredentialsSecure) {
  expectSessionToken("TOKEN");
  expectCredentialListingSecure("doc1");
  expectDocumentSecure(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, CredentialExpiration) {
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
  const auto credentials = provider_.getCredentials();
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
  const auto new_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, CredentialExpirationSecure) {
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
  const auto credentials = provider_.getCredentials();
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
  const auto new_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}

class TaskRoleCredentialsProviderTest : public testing::Test {
public:
  TaskRoleCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)),
        provider_(
            *api_,
            [this](Http::RequestMessage& message) -> absl::optional<std::string> {
              return this->fetch_metadata_.fetch(message);
            },
            "169.254.170.2:80/path/to/doc", "auth_token") {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void expectDocument(const absl::optional<std::string>& document) {
    Http::TestRequestHeaderMapImpl headers{{":path", "/path/to/doc"},
                                           {":authority", "169.254.170.2:80"},
                                           {":method", "GET"},
                                           {"authorization", "auth_token"}};
    EXPECT_CALL(fetch_metadata_, fetch(messageMatches(headers))).WillOnce(Return(document));
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  TaskRoleCredentialsProvider provider_;
};

TEST_F(TaskRoleCredentialsProviderTest, FailedFetchingDocument) {
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, MalformedDocumenet) {
  expectDocument(R"EOF(
not json
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, EmptyValues) {
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": "",
  "Expiration": ""
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, FullCachedCredentials) {
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:05:00Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, NormalCredentialExpiration) {
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2019-01-02T03:04:05Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
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
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, TimestampCredentialExpiration) {
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "2018-01-02T03:04:05Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
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
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

class DefaultCredentialsProviderChainTest : public testing::Test {
public:
  DefaultCredentialsProviderChainTest() : api_(Api::createApiForTest(time_system_)) {
    EXPECT_CALL(factories_, createEnvironmentCredentialsProvider());
  }

  ~DefaultCredentialsProviderChainTest() override {
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_EC2_METADATA_DISABLED");
  }

  class MockCredentialsProviderChainFactories : public CredentialsProviderChainFactories {
  public:
    MOCK_METHOD(CredentialsProviderSharedPtr, createEnvironmentCredentialsProvider, (), (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createCredentialsFileCredentialsProvider, (Api::Api&),
                (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createTaskRoleCredentialsProvider,
                (Api::Api&, const MetadataCredentialsProviderBase::MetadataFetcher&,
                 absl::string_view, absl::string_view),
                (const));
    MOCK_METHOD(CredentialsProviderSharedPtr, createInstanceProfileCredentialsProvider,
                (Api::Api&, const MetadataCredentialsProviderBase::MetadataFetcher& fetcher),
                (const));
  };

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<MockCredentialsProviderChainFactories> factories_;
};

TEST_F(DefaultCredentialsProviderChainTest, NoEnvironmentVars) {
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, CredentialsFileDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.enable_aws_credentials_file", "false"}});

  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_))).Times(0);
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _)).Times(0);
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataNotDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "false", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, RelativeUri) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(Ref(*api_), _,
                                                            "169.254.170.2:80/path/to/creds", ""));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriNoAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_,
              createTaskRoleCredentialsProvider(Ref(*api_), _, "http://host/path/to/creds", ""));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriWithAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  EXPECT_CALL(factories_, createCredentialsFileCredentialsProvider(Ref(*api_)));
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(
                              Ref(*api_), _, "http://host/path/to/creds", "auth_token"));
  DefaultCredentialsProviderChain chain(*api_, DummyMetadataFetcher(), factories_);
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
