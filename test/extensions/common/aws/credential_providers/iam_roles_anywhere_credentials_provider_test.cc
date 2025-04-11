#include <cstddef>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "gtest/gtest.h"

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::Eq;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// Example certificates generated for test cases
std::string server_root_cert_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDczCCAlugAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADAT
MREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTgwMDIz
MzdaMFoxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNV
BAoME0RlZmF1bHQgQ29tcGFueSBMdGQxFjAUBgNVBAMMDXRlc3QtcnNhLnRlc3Qw
ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEevBD9Ayp+kQL/uXsgB
Zk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/q8px6nX+3RHWRyA
X0B58jYHUaan+IB/7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8
ZVtbSTZbkJuRaKB1AFEc/aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZ
y5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/modQdtAVeIeiwW1riKZQxGle
d+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+a95AiamQpKoqoRJ
AgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAUQeJl/2AyjV/Z7KCl0/rs
sN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1UdDwEB/wQE
AwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQEL
BQADggEBAAHbzqfQ/3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/RqP
Xwk+oTBrsNG+CeYpXpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9t
juygVLvPI2RugWVQ+0+tTtA+PPbwuwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiT
vXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNgz/3F8hDfHkjQU8fErucVLOUw
C7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOODCoF7m/S7T7Gi
Y8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk=
-----END CERTIFICATE-----
)EOF";

std::string server_root_private_key_rsa_pem = R"EOF(
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCqMEevBD9Ayp+k
QL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/q8px6nX
+3RHWRyAX0B58jYHUaan+IB/7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YN
p+KU6yb8ZVtbSTZbkJuRaKB1AFEc/aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp
7k2OVeIZy5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/modQdtAVeIeiwW1r
iKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+a95Aiam
QpKoqoRJAgMBAAECggEAAkzrhSM9rySmBoh9B632jmVJf/3wj1Bjun15wJi67dWC
h6cWBsYTnacryUFmbyMwEbcwSgkViU8qfbHHkzjSRK507skOP6hUBEB8zS3ncllP
uW2NXlCV94k9CKTAZYyFiIjpC15SzgLReuUGcCyjDuWYV+osDs4MOkmTpK07y3Rh
HOCRoPcjbboyiP/7OS8XvTC+WojNmLBM2GFyXe3zw75I8qXglLjp2s+aKzszo9TN
k2YCCd90PxiqbII2A4TYUB+utRpZ2p4QH5l2+X2EOHwf96qFhRpPKSJDpQp+hsm1
OTitFJWrRzBwG862ET0Mm14KEmUynv462YKz2mMHAQKBgQDPze2dDAzEJqou41e1
SX7klqPMiI/Cr2fL64G4PdoXVf8SmdA69sWksvlv2+TIzA66dVbDq0fMJYOj3hPM
EDB5x7mQEmqR5yfW+rA7xNMxfPSK4deAFbuXZWQbNPHr+3fmcoz9MNyu0mz26t3H
F5Ib42M6tY5kZzGzQT79W7t7SQKBgQDRqPhM154Mt8ow80lLiUES7NeDsCmUSXw6
RwSo2zXER05w4DMYfmWHcXZ/LIMd9HnY2nUFejoB2P8JGkLgB6zbFH+aBNgBX5V4
6vxx7bp3x/utxSdc1f39697BuXrtv6qUn4I4NyIm/rj/vqBDXNRj79J1utdd8U+c
d/aVGBXBAQKBgQCMA0cvQpgzbY3LC9jjyAJciHcS74xVc5PvHN4JQnt4r7OuV76q
i+y9PO2+BZ4QARWHYlo0empkzX314kLagqn207Bet1ngtqvsOHqXutVFidjG3sYx
gfMkXedmQXUjOAsgVVxTmCGJFTTf5X3KkEIc0kfgncW0NqeRDMwhLzaSKQKBgBxN
cg91f/l5igrnnLpcsfMrE8DMNCC3dtSrJ57f0LdJZPZp3Zvt3CjXkUaDrMOLcDNs
8iUmJdSABZWl/OcfQh9k+gDBrKMq0xO6rQ94JxbqYThJCBJJNPtlLvH55vVXTWC4
06xhDPQ0qKalhh7x1h4TjtajvVUKMVQPAbOIx88BAoGANUyfP2m4QdtHR3hOE8W8
U+p2qcbT0/02ggwwdKALqlmUaWgljoG8rarmV8ISc82j2B9fEgJR2z5frA/EKzV5
RBqAC6sxyAYn2wbzuyINJdSLpehQKDkKxEnO4QLodClHYV1F9AlAfbSLmIlRFv/y
1mNT8ElsYLTkJr2AqGNZOlQ=
-----END PRIVATE KEY-----
)EOF";

std::string server_root_chain_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIC8zCCAdugAwIBAgIRAKPuvF4B35jaiV9QrXSH9hIwDQYJKoZIhvcNAQELBQAw
EzERMA8GA1UEAwwIdGVzdC1yc2EwHhcNMjQxMTE3MjMyMzMzWhcNMzQxMTE4MDAy
MzMxWjATMREwDwYDVQQDDAh0ZXN0LXJzYTCCASIwDQYJKoZIhvcNAQEBBQADggEP
ADCCAQoCggEBAK+Om/cFBnAM8hytvuKf+sIjjqzQcdedKpk9Jq4T5f8E/arlFQws
CulSLt7Cb45yJ9J6DuiLiFf4o31eCtwrZn47KZcysWmG5Q992OI0vdQgeQMidbUV
DXmcv20iawx3FKoU/LIrnFkLlKWivhbjrAX9pgiL2bQeMvlt/oISevlUi0SYkeCJ
l2X5hVOTnRmCDHoeWUegRoZLSkPH/eQBsNwkVY+2IxwXcRCxozFjV1wPcoNPp0+2
KwGAJ16oUUum9SqIUFcZx9DB3gnIvAu9BxgLJ6KSvs64DVDmWcrHpo+ZU9JsKeBI
9GXOOrCaYZwglxLt7GY6OvNdWPCabkPhJCcCAwEAAaNCMEAwDwYDVR0TAQH/BAUw
AwEB/zAdBgNVHQ4EFgQUQeJl/2AyjV/Z7KCl0/rssN6mws8wDgYDVR0PAQH/BAQD
AgGGMA0GCSqGSIb3DQEBCwUAA4IBAQBYxHCbtZZGpOtLYIvTnGJDns8D0Hc3eaoi
fmhmIPEdmcmxlHXqKyh5HyRdQ+sklNfVdjxBmwQDE96Tx9o3q3Xdfp2AHEaupzQx
XYtl50W49OuHzelXicOZY7aVe5ixb4l8m5UT7bO3A6RG+qgL1SRZbVftDGO2NxFe
iO8Kg7h3ti564Vv6I4SNoZEjkDKg8NWfDN1NYqd2FN1Crzida5RZf5fIXxnjAmnX
ubewSfcSGN98K+IREpCWSbf9DMkaeHx6Sw85UjovZU2KgedQHkQ0bhsXqY1PDlP3
WgTQAfHx04TA8rljw5lyGxOZJQ3WIvsc4qCn2Q1Dv+AjpLNZq411
-----END CERTIFICATE-----
)EOF";

class MessageMatcher : public testing::MatcherInterface<Http::RequestMessage&> {
public:
  explicit MessageMatcher(Http::RequestMessage& expected_message)
      : expected_message_(expected_message) {}

  bool MatchAndExplain(Http::RequestMessage& message,
                       testing::MatchResultListener* result_listener) const override {
    bool equal =
        TestUtility::headerMapEqualIgnoreOrder(message.headers(), expected_message_.headers());
    if (!equal) {
      *result_listener << "\n"
                       << TestUtility::addLeftAndRightPadding("Expected header map:") << "\n"
                       << expected_message_.headers()
                       << TestUtility::addLeftAndRightPadding("is not equal to actual header map:")
                       << "\n"
                       << message.headers()
                       << TestUtility::addLeftAndRightPadding("") // line full of padding
                       << "\n";
    }
    if (!expected_message_.bodyAsString().empty()) {
      if (message.bodyAsString() != expected_message_.bodyAsString()) {
        equal = 0;
        *result_listener << "\n"
                         << TestUtility::addLeftAndRightPadding("Expected message body:") << "\n"
                         << expected_message_.bodyAsString()
                         << TestUtility::addLeftAndRightPadding(
                                "is not equal to actual message body:")
                         << "\n"
                         << message.bodyAsString()
                         << TestUtility::addLeftAndRightPadding("") // line full of padding
                         << "\n";
      }
    }
    return equal;
  }

  void DescribeTo(::std::ostream* os) const override { *os << "Message matches"; }

  void DescribeNegationTo(::std::ostream* os) const override { *os << "Message does not match"; }

private:
  Http::RequestMessage& expected_message_;
};

testing::Matcher<Http::RequestMessage&> messageMatches(Http::RequestMessage& expected_message) {
  return testing::MakeMatcher(new MessageMatcher(expected_message));
}

class IamRolesAnywhereCredentialsProviderTest : public testing::Test {
public:
  IamRolesAnywhereCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }
  ~IamRolesAnywhereCredentialsProviderTest() override = default;

  void setupProvider(std::string cert, std::string pkey, std::string chain = "",
                     std::string session = "session", uint16_t duration = 3600) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));

    auto cert_env = std::string("CERT");

    std::string yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                                   cert_env);

    TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source_);

    TestEnvironment::setEnvVar(cert_env, cert, 1);

    auto pkey_env = std::string("PKEY");
    TestEnvironment::setEnvVar(pkey_env, pkey, 1);
    yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                       pkey_env);

    TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source_);

    auto chain_env = std::string("CHAIN");
    TestEnvironment::setEnvVar(chain_env, chain, 1);
    yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                       chain_env);

    TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source_);

    iam_roles_anywhere_config_.mutable_certificate_chain()->set_environment_variable("CHAIN");
    iam_roles_anywhere_config_.mutable_private_key()->set_environment_variable("PKEY");
    iam_roles_anywhere_config_.mutable_certificate()->set_environment_variable("CERT");
    iam_roles_anywhere_config_.set_role_session_name(session);
    iam_roles_anywhere_config_.mutable_session_duration()->set_seconds(duration);
    iam_roles_anywhere_config_.set_role_arn("arn:role-arn");
    iam_roles_anywhere_config_.set_profile_arn("arn:profile-arn");
    iam_roles_anywhere_config_.set_trust_anchor_arn("arn:trust-anchor-arn");
    mock_manager_ = std::make_shared<MockAwsClusterManager>();
    base_manager_ = std::dynamic_pointer_cast<AwsClusterManager>(mock_manager_);

    manager_optref_.emplace(base_manager_);
    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("rolesanywhere.ap-southeast-2.amazonaws.com:443"));

    const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
    const auto initialization_timer = std::chrono::seconds(2);

    auto roles_anywhere_certificate_provider =
        std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
            context_, iam_roles_anywhere_config_.certificate(),
            iam_roles_anywhere_config_.private_key(),
            iam_roles_anywhere_config_.certificate_chain());
    // Create our own x509 signer just for IAM Roles Anywhere
    auto roles_anywhere_signer =
        std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
            absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
            roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());

    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        context_, manager_optref_, "rolesanywhere.ap-southeast-2.amazonaws.com",
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        "ap-southeast-2", refresh_state, initialization_timer, std::move(roles_anywhere_signer),
        iam_roles_anywhere_config_);
  }

  Event::DispatcherPtr setupDispatcher() {
    auto dispatcher = std::make_unique<Event::MockDispatcher>();
    EXPECT_CALL(*dispatcher, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new Filesystem::MockWatcher();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::Modified, _))
          .WillRepeatedly(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                watch_cbs_.push_back(cb);
                return absl::OkStatus();
              }));
      return mock_watcher;
    }));

    return dispatcher;
  }

  void expectDocument(const uint64_t status_code, const std::string&& document,
                      Http::RequestMessage& message) {

    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(message), _, _))
        .WillRepeatedly(Invoke([this, status_code, document = std::move(document)](
                                   Http::RequestMessage&, Tracing::Span&,
                                   MetadataFetcher::MetadataReceiver& receiver) {
          if (status_code == enumToInt(Http::Code::OK) ||
              status_code == enumToInt(Http::Code::Created)) {
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

  Http::TestRequestHeaderMapImpl rsa_headers_nochain_{
      {":path", "/sessions"},
      {":authority", "rolesanywhere.ap-southeast-2.amazonaws.com"},
      {":scheme", "https"},
      {":method", "POST"},
      {"content-type", "application/json"},
      {"authorization",
       "AWS4-X509-RSA-SHA256 "
       "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
       "aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509, "
       "Signature="
       "0b296dd7b4cdaede1ce25a305884e1630955a08f665eab2f011d1456d7cca57abfc2d537fe81c731fef0fbcee97"
       "5ad8891d342c28046779328530810f929edfe9eb3769c7d44b741a381ff15a50983e60d761f23b979ca36c33b50"
       "a61a1c3cb29a30e19c0a14f5660a3d3cab7ceb58b0dc97e0d256fac15f1b7635ca12e9b598b4f5bed7863015a49"
       "c0a1e2f72e847075a7515f538508293af64c5d30ecbb13d4a051efa728ae0d35c4e1920d90f6e4376c7477d8d01"
       "f3e71c5288dc1eb69e9ecb61767cceb92942f677437bad4eb4ffff6dfac1aac2d6b76318f47fe66e9d34931b2d3"
       "3c90b340ca97a0a3f5b9896f9834109f83b6c21f5c3a281dcb73263b0"},
      {"x-amz-date", "20180102T030405Z"},
      {"x-amz-content-sha256", "ba4c787c2a8f206d06d204e6046e05f4805ed9d401c3161a2a463703014a03a1"},
      {"x-amz-x509",
       "MIIDczCCAlugAwIBAgIQYy0lLc2af47/"
       "u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTg"
       "wMDIzMzdaMFoxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcG"
       "FueSBMdGQxFjAUBgNVBAMMDXRlc3QtcnNhLnRlc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEevB"
       "D9Ayp+kQL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/"
       "q8px6nX+3RHWRyAX0B58jYHUaan+IB/"
       "7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8ZVtbSTZbkJuRaKB1AFEc/"
       "aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZy5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/"
       "modQdtAVeIeiwW1riKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+"
       "a95AiamQpKoqoRJAgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAUQeJl/2AyjV/Z7KCl0/"
       "rssN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1UdDwEB/"
       "wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQADggEBAAHbzqfQ/"
       "3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/"
       "RqPXwk+oTBrsNG+CeYpXpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9tjuygVLvPI2RugWVQ+0+tTtA+"
       "PPbwuwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiTvXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNgz/"
       "3F8hDfHkjQU8fErucVLOUwC7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOODCoF7m/"
       "S7T7GiY8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk="}};

  Http::TestRequestHeaderMapImpl rsa_headers_chain_{
      {":path", "/sessions"},
      {":authority", "rolesanywhere.ap-southeast-2.amazonaws.com"},
      {":scheme", "https"},
      {":method", "POST"},
      {"content-type", "application/json"},
      {"x-amz-date", "20180102T030405Z"},
      {"x-amz-x509",
       "MIIDczCCAlugAwIBAgIQYy0lLc2af47/"
       "u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTg"
       "wMDIzMzdaMFoxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcG"
       "FueSBMdGQxFjAUBgNVBAMMDXRlc3QtcnNhLnRlc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEevB"
       "D9Ayp+kQL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/"
       "q8px6nX+3RHWRyAX0B58jYHUaan+IB/"
       "7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8ZVtbSTZbkJuRaKB1AFEc/"
       "aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZy5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/"
       "modQdtAVeIeiwW1riKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+"
       "a95AiamQpKoqoRJAgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAUQeJl/2AyjV/Z7KCl0/"
       "rssN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1UdDwEB/"
       "wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQADggEBAAHbzqfQ/"
       "3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/"
       "RqPXwk+oTBrsNG+CeYpXpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9tjuygVLvPI2RugWVQ+0+tTtA+"
       "PPbwuwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiTvXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNgz/"
       "3F8hDfHkjQU8fErucVLOUwC7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOODCoF7m/"
       "S7T7GiY8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk="},
      {"x-amz-x509-chain",
       "MIIC8zCCAdugAwIBAgIRAKPuvF4B35jaiV9QrXSH9hIwDQYJKoZIhvcNAQELBQAwEzERMA8GA1UEAwwIdGVzdC1yc2E"
       "wHhcNMjQxMTE3MjMyMzMzWhcNMzQxMTE4MDAyMzMxWjATMREwDwYDVQQDDAh0ZXN0LXJzYTCCASIwDQYJKoZIhvcNAQ"
       "EBBQADggEPADCCAQoCggEBAK+Om/cFBnAM8hytvuKf+sIjjqzQcdedKpk9Jq4T5f8E/"
       "arlFQwsCulSLt7Cb45yJ9J6DuiLiFf4o31eCtwrZn47KZcysWmG5Q992OI0vdQgeQMidbUVDXmcv20iawx3FKoU/"
       "LIrnFkLlKWivhbjrAX9pgiL2bQeMvlt/oISevlUi0SYkeCJl2X5hVOTnRmCDHoeWUegRoZLSkPH/"
       "eQBsNwkVY+2IxwXcRCxozFjV1wPcoNPp0+2KwGAJ16oUUum9SqIUFcZx9DB3gnIvAu9BxgLJ6KSvs64DVDmWcrHpo+"
       "ZU9JsKeBI9GXOOrCaYZwglxLt7GY6OvNdWPCabkPhJCcCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/"
       "zAdBgNVHQ4EFgQUQeJl/2AyjV/Z7KCl0/rssN6mws8wDgYDVR0PAQH/"
       "BAQDAgGGMA0GCSqGSIb3DQEBCwUAA4IBAQBYxHCbtZZGpOtLYIvTnGJDns8D0Hc3eaoifmhmIPEdmcmxlHXqKyh5HyR"
       "dQ+sklNfVdjxBmwQDE96Tx9o3q3Xdfp2AHEaupzQxXYtl50W49OuHzelXicOZY7aVe5ixb4l8m5UT7bO3A6RG+"
       "qgL1SRZbVftDGO2NxFeiO8Kg7h3ti564Vv6I4SNoZEjkDKg8NWfDN1NYqd2FN1Crzida5RZf5fIXxnjAmnXubewSfcS"
       "GN98K+"
       "IREpCWSbf9DMkaeHx6Sw85UjovZU2KgedQHkQ0bhsXqY1PDlP3WgTQAfHx04TA8rljw5lyGxOZJQ3WIvsc4qCn2Q1Dv"
       "+AjpLNZq411"},
      {"x-amz-content-sha256", "ba4c787c2a8f206d06d204e6046e05f4805ed9d401c3161a2a463703014a03a1"},
      {"authorization",
       "AWS4-X509-RSA-SHA256 "
       "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
       "aws4_request, "
       "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509;x-amz-x509-"
       "chain, "
       "Signature="
       "273f78e504db3ef6326a5b912e9990c2acb2e6a7aaee9c201ac6bac75798daaf253b8da955bf1124f528f116fd1"
       "3f240387563d8e9d1661cdcff6b830b877e1e5e3bbd3256afdf1ccfc6f090ee60af3bb5f0bfd515023506d42bdd"
       "5e9aa401651b6e1a50d879c54e9101966c097b6a9583ac9a175da14e4fe2a7af310402e658ed993228c47170c49"
       "e1c860632f51ea79def4cafb66cc22ae4d27078673b1f3bd6854b80f3b04499faab731cec06477640687f6b196a"
       "0954521caeba9bd6edc56b28bf91af9bcab86f47026b35d905f0b8dc8af40d79c27163aa9331f1c99e47f1f8b75"
       "754701992be356cca96f9925b374fd2f300f40b4eece35719d2d4d8fe"}};

  Http::TestRequestHeaderMapImpl rsa_headers_chain_fast_forward_{
      {":path", "/sessions"},
      {":authority", "rolesanywhere.ap-southeast-2.amazonaws.com"},
      {":scheme", "https"},
      {":method", "POST"},
      {"content-type", "application/json"},
      {"x-amz-date", "20180102T050405Z"},
      {"x-amz-x509",
       "MIIDczCCAlugAwIBAgIQYy0lLc2af47/"
       "u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTg"
       "wMDIzMzdaMFoxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcG"
       "FueSBMdGQxFjAUBgNVBAMMDXRlc3QtcnNhLnRlc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEevB"
       "D9Ayp+kQL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/"
       "q8px6nX+3RHWRyAX0B58jYHUaan+IB/"
       "7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8ZVtbSTZbkJuRaKB1AFEc/"
       "aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZy5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/"
       "modQdtAVeIeiwW1riKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+"
       "a95AiamQpKoqoRJAgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAUQeJl/2AyjV/Z7KCl0/"
       "rssN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1UdDwEB/"
       "wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQADggEBAAHbzqfQ/"
       "3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/"
       "RqPXwk+oTBrsNG+CeYpXpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9tjuygVLvPI2RugWVQ+0+tTtA+"
       "PPbwuwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiTvXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNgz/"
       "3F8hDfHkjQU8fErucVLOUwC7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOODCoF7m/"
       "S7T7GiY8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk="},
      {"x-amz-x509-chain",
       "MIIC8zCCAdugAwIBAgIRAKPuvF4B35jaiV9QrXSH9hIwDQYJKoZIhvcNAQELBQAwEzERMA8GA1UEAwwIdGVzdC1yc2E"
       "wHhcNMjQxMTE3MjMyMzMzWhcNMzQxMTE4MDAyMzMxWjATMREwDwYDVQQDDAh0ZXN0LXJzYTCCASIwDQYJKoZIhvcNAQ"
       "EBBQADggEPADCCAQoCggEBAK+Om/cFBnAM8hytvuKf+sIjjqzQcdedKpk9Jq4T5f8E/"
       "arlFQwsCulSLt7Cb45yJ9J6DuiLiFf4o31eCtwrZn47KZcysWmG5Q992OI0vdQgeQMidbUVDXmcv20iawx3FKoU/"
       "LIrnFkLlKWivhbjrAX9pgiL2bQeMvlt/oISevlUi0SYkeCJl2X5hVOTnRmCDHoeWUegRoZLSkPH/"
       "eQBsNwkVY+2IxwXcRCxozFjV1wPcoNPp0+2KwGAJ16oUUum9SqIUFcZx9DB3gnIvAu9BxgLJ6KSvs64DVDmWcrHpo+"
       "ZU9JsKeBI9GXOOrCaYZwglxLt7GY6OvNdWPCabkPhJCcCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/"
       "zAdBgNVHQ4EFgQUQeJl/2AyjV/Z7KCl0/rssN6mws8wDgYDVR0PAQH/"
       "BAQDAgGGMA0GCSqGSIb3DQEBCwUAA4IBAQBYxHCbtZZGpOtLYIvTnGJDns8D0Hc3eaoifmhmIPEdmcmxlHXqKyh5HyR"
       "dQ+sklNfVdjxBmwQDE96Tx9o3q3Xdfp2AHEaupzQxXYtl50W49OuHzelXicOZY7aVe5ixb4l8m5UT7bO3A6RG+"
       "qgL1SRZbVftDGO2NxFeiO8Kg7h3ti564Vv6I4SNoZEjkDKg8NWfDN1NYqd2FN1Crzida5RZf5fIXxnjAmnXubewSfcS"
       "GN98K+"
       "IREpCWSbf9DMkaeHx6Sw85UjovZU2KgedQHkQ0bhsXqY1PDlP3WgTQAfHx04TA8rljw5lyGxOZJQ3WIvsc4qCn2Q1Dv"
       "+AjpLNZq411"},
      {"x-amz-content-sha256", "ba4c787c2a8f206d06d204e6046e05f4805ed9d401c3161a2a463703014a03a1"},
      {"authorization",
       "AWS4-X509-RSA-SHA256 "
       "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
       "aws4_request, "
       "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509;x-amz-x509-"
       "chain, "
       "Signature="
       "77e20f5d7d48f966adb3f3bd640bd9656edb6a4823f9217f8f7e699544b14367096ecad7d9f99580047de151750"
       "1b9895b152e5ff735c1ac7a9057a576fd54d63cf07ad9a0638486ef1289c87a43a5b84c522b9dbb44fba43eda97"
       "e593a8d145640e1b071877449b67545c57d8652e37c00bd0ef122ade548c28650592831410372dd8bc5d70db482"
       "b5d79818d148e71f031ea2588b519f6eb83f0320b39e0ebb26e56e8b0cc60a338d2291c1296b5c6423c3af2fed8"
       "7998ec1da571646afdb83dc0e8a9baa456ad0794d49e21e79011397d3d8418b470d5055c6dc27588cb16041a3ad"
       "025633ed3b88a5ec5fcc4a5ef1bf7fceb6da83ab27a94b584cea9e1ae"}};

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  std::vector<Filesystem::Watcher::OnChangedCb> watch_cbs_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<MockFetchMetadata> fetch_metadata_;
  MockMetadataFetcher* raw_metadata_fetcher_;
  MetadataFetcherPtr metadata_fetcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  IAMRolesAnywhereCredentialsProviderPtr provider_ = nullptr;
  envoy::config::core::v3::DataSource certificate_data_source_, private_key_data_source_,
      cert_chain_data_source_;
  Event::MockTimer* timer_{};
  OptRef<std::shared_ptr<AwsClusterManager>> manager_optref_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  std::shared_ptr<AwsClusterManager> base_manager_;
  envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider iam_roles_anywhere_config_;
};

// Test cases created from python implementation of iam roles anywhere session
TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigning) {
  // This is what we expect to see requested by the signer
  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "");
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningInvalidChainOk) {

  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "abc");
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningCustomSessionName) {

  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};

  Http::RequestMessageImpl message(std::move(headers));
  message.headers().setCopy(
      Http::LowerCaseString("authorization"),
      "AWS4-X509-RSA-SHA256 "
      "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
      "aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509, "
      "Signature="
      "1b578c0283f15a0ef61dbf967877cbbad1bbbae056921e336bafafa38c466f38d0ad5c39a45f70728064fb1bae21"
      "0db085d35c2a8f810dcd94adc6e78cccd79e012bfc675cbfc7149e07ecf9464dd985663bab91350ce8b120204ce3"
      "99858f6c30696e3f8e409bc6b23a4c377f1c11f409d31fb7732028bb73985ed030e3ae4670b4c5877fd00d17b6e3"
      "424fb1f7070ea078bf598082e0810c9b9fa0b7b54e248f8bd51494fc9ad8c8a6d86e253bb0d7b7a17d17cf22b0c9"
      "ca25df7112bf90c31d6c47e2ecf1bd43e9137dadcbd1c6c65ac59c84ab723b10679a58faba0fac996f638950e1e9"
      "b722215c37d42d2e364e0be234bd46704e44938fb904804f0ca7");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "229d2f52a6b6d64706c163c3abc8f19b5e661d9a8d6d4ec684b1e71f14edf7e5");

  message.body().add("{\"durationSeconds\": 3600, \"profileArn\": \"arn:profile-arn\", "
                     "\"roleArn\": \"arn:role-arn\", \"trustAnchorArn\": \"arn:trust-anchor-arn\", "
                     "\"roleSessionName\": \"mysession\"}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "abc", "mysession");

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningBlankSessionName) {

  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};

  Http::RequestMessageImpl message(std::move(headers));
  message.headers().setCopy(
      Http::LowerCaseString("authorization"),
      "AWS4-X509-RSA-SHA256 "
      "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
      "aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509, "
      "Signature="
      "09bee21035a040e96194dc42d7e48bacc0c6be47d226be00501b3ce6389227b6b2ad67321d0b87c673bd51554e2c"
      "86571b7ba6aae628bcba8d2b587d5108474f969ee168b1fb4d8d0dfac8b8cbabfeb3ad0a2d80f87eaae3afbbfeea"
      "d69d6f9a53fab98a8bdc72613fb4cea898c9d5d0e0a6c5806a46c7833e0acb8992d8f0ef0d0498acacb6cc81c90b"
      "0bfa50aed7814f24b3b6509fdbe6181e1218650784829cc331270c19982c8924434fc52f92c19037ba0aa790ad4d"
      "6dbb5e66cd017a066fc0fb058b04a711fa5f91e25e748ce44be98d9ce4b0665726b8ca93ccad1f87f8f3e4079472"
      "b321306c2320038828ff21703e08b1a77a696d3e5e326cdcf50a");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "3351f0119309ca7d266e49d34647662de4053d521f17227958be5f4d44f014a8");

  message.body().add(
      "{\"durationSeconds\": 3600, \"profileArn\": \"arn:profile-arn\", \"roleArn\": "
      "\"arn:role-arn\", \"trustAnchorArn\": \"arn:trust-anchor-arn\"}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "abc", "");

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningCustomDuration) {

  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};

  Http::RequestMessageImpl message(std::move(headers));
  message.headers().setCopy(
      Http::LowerCaseString("authorization"),
      "AWS4-X509-RSA-SHA256 "
      "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
      "aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509, "
      "Signature="
      "369ec50ce45ec2586dc9adc394c46bc2bb06fc31141047a094eaad6c97619e9217f1b19d2941dbd1a4396edfeed0"
      "fbf35b93faf414275de8d798d86557a3bb09ce6d0ce62e642e551076def486ce338c31d9b5e4e5f8d4d6396c2afb"
      "e523df533fa5f1c3f2056bd06950b06edd4f9abd18057df395cb17eaec4a6a5571848dc380d598f26e4051b0bc82"
      "197da01d760e29dd13c960425a823887c6b3fb16d416fca59b5b1bc2cbeca7275977a665c8bca101b8de81c38993"
      "e57bc8949afbbcd5ff88df52e796f45cd254b6441cc62f76acf4a7c632e2963ad9d8f8c4329a03c97be56109faf0"
      "a5ea4e7619c052330a83da8fbf6c475c353adf621a307eab1271");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "7458c6feacdabcbde5c0a5b3058497c47e7f48530efde3d1fab51329081b2fdd");

  message.body().add("{\"durationSeconds\": 123, \"profileArn\": \"arn:profile-arn\", \"roleArn\": "
                     "\"arn:role-arn\", \"trustAnchorArn\": \"arn:trust-anchor-arn\", "
                     "\"roleSessionName\": \"mysession\"}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "abc", "mysession", 123);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningWithChain) {

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, CredentialExpiration) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, R"EOF(
{
  "credentialSet": [
    {
      "credentials": {
        "accessKeyId": "akid",
        "secretAccessKey": "secret",
        "sessionToken": "token",
        "expiration": "2018-01-02T03:14:05Z"
      }
    }
  ]
}
 )EOF",
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::minutes(10)), nullptr))
      .Times(2);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.advanceTimeWait(std::chrono::hours(2));

  auto headers2 = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{rsa_headers_chain_fast_forward_}};
  Http::RequestMessageImpl message2(std::move(headers2));

  expectDocument(201, R"EOF(
{
  "credentialSet": [
    {
      "credentials": {
        "accessKeyId": "new_akid",
        "secretAccessKey": "new_secret",
        "sessionToken": "new_token",
        "expiration": "2018-01-02T05:14:05Z"
      }
    }
  ]
}
 )EOF",
                 message2);
  // Timer will have been advanced by ten minutes, so check that firing it will refresh the
  // credentials
  EXPECT_CALL(*raw_metadata_fetcher_, cancel());
  timer_->invokeCallback();
  const auto new_credentials = provider_->getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, InvalidExpiration) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, R"EOF(
{
  "credentialSet": [
    {
      "credentials": {
        "accessKeyId": "akid",
        "secretAccessKey": "secret",
        "sessionToken": "token",
        "expiration": "2017-01-02T03:14:05Z"
      }
    }
  ]
}
 )EOF",
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3595)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadJsonResponse) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, R"EOF(
{
 invalidJson
}
 )EOF",
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadCredentialSetValue) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, R"EOF(
{
  "credentialSet": 1
}
 )EOF",
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadCredentialSetArray) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, R"EOF(
{
  "credentialSet": [
    {
      "invalid":1
    }
  ]
}
 )EOF",
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, EmptyJsonResponse) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();

  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, SignerFails) {
  // Should return no credentials when the internal signer fails

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));

  auto cert_env = std::string("CERT");

  std::string yaml = fmt::format(R"EOF(
  environment_variable: "{}"
)EOF",
                                 cert_env);

  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source_);

  TestEnvironment::setEnvVar(cert_env, server_root_cert_rsa_pem, 1);

  auto pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_root_private_key_rsa_pem, 1);
  yaml = fmt::format(R"EOF(
  environment_variable: "{}"
)EOF",
                     pkey_env);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source_);

  auto chain_env = std::string("CHAIN");
  TestEnvironment::setEnvVar(chain_env, server_root_chain_rsa_pem, 1);
  yaml = fmt::format(R"EOF(
  environment_variable: "{}"
)EOF",
                     chain_env);

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source_);

  iam_roles_anywhere_config_.mutable_certificate_chain()->set_environment_variable("CHAIN");
  iam_roles_anywhere_config_.mutable_private_key()->set_environment_variable("PKEY");
  iam_roles_anywhere_config_.mutable_certificate()->set_environment_variable("CERT");
  iam_roles_anywhere_config_.set_role_session_name("session");
  iam_roles_anywhere_config_.mutable_session_duration()->set_seconds(3600);
  iam_roles_anywhere_config_.set_role_arn("arn:role-arn");
  iam_roles_anywhere_config_.set_profile_arn("arn:profile-arn");
  iam_roles_anywhere_config_.set_trust_anchor_arn("arn:trust-anchor-arn");
  ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  base_manager_ = std::dynamic_pointer_cast<AwsClusterManager>(mock_manager_);

  manager_optref_.emplace(base_manager_);
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
      .WillRepeatedly(Return("rolesanywhere.ap-southeast-2.amazonaws.com:443"));

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto roles_anywhere_certificate_provider =
      std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
          context_, iam_roles_anywhere_config_.certificate(),
          iam_roles_anywhere_config_.private_key(), iam_roles_anywhere_config_.certificate_chain());

  std::unique_ptr<MockIAMRolesAnywhereSigV4Signer> mock_signer =
      std::make_unique<MockIAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());
  auto* mock_ptr = mock_signer.get();

  // Force signing to fail
  EXPECT_CALL(*mock_ptr, sign(_, true, _)).WillOnce(Return(absl::InvalidArgumentError("error")));

  provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
      context_, manager_optref_, "rolesanywhere.ap-southeast-2.amazonaws.com",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      "ap-southeast-2", refresh_state, initialization_timer, std::move(mock_signer),
      iam_roles_anywhere_config_);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();

  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
  delete (raw_metadata_fetcher_);
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, Coverage) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));
  expectDocument(201, "", message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  EXPECT_TRUE(provider_friend.needsRefresh());
}

class IamRolesAnywhereCredentialsProviderBadCredentialsTest : public testing::Test {
public:
  IamRolesAnywhereCredentialsProviderBadCredentialsTest() : api_(Api::createApiForTest()){};

  envoy::config::core::v3::DataSource certificate_data_source_, private_key_data_source_,
      cert_chain_data_source_;
  MetadataFetcherPtr metadata_fetcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::shared_ptr<IAMRolesAnywhereCredentialsProvider> provider_;
  Api::ApiPtr api_;

  void setupProvider(std::string cert, std::string pkey, std::string chain = "") {

    auto cert_env = std::string("CERT");

    std::string yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                                   cert_env);

    TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source_);

    TestEnvironment::setEnvVar(cert_env, cert, 1);

    auto pkey_env = std::string("PKEY");
    TestEnvironment::setEnvVar(pkey_env, pkey, 1);
    yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                       pkey_env);

    TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source_);

    auto chain_env = std::string("CHAIN");
    TestEnvironment::setEnvVar(chain_env, chain, 1);
    yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                       chain_env);

    TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source_);

    iam_roles_anywhere_config_.mutable_certificate_chain()->set_environment_variable("CHAIN");
    iam_roles_anywhere_config_.mutable_private_key()->set_environment_variable("PKEY");
    iam_roles_anywhere_config_.mutable_certificate()->set_environment_variable("CERT");
    mock_manager_ = std::make_shared<MockAwsClusterManager>();
    base_manager_ = std::dynamic_pointer_cast<AwsClusterManager>(mock_manager_);

    manager_optref_.emplace(base_manager_);

    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("rolesanywhere.ap-southeast-2.amazonaws.com/sessions"));

    const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
    const auto initialization_timer = std::chrono::seconds(2);
    auto roles_anywhere_certificate_provider =
        std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
            context_, iam_roles_anywhere_config_.certificate(),
            iam_roles_anywhere_config_.private_key(),
            iam_roles_anywhere_config_.certificate_chain());
    // Create our own x509 signer just for IAM Roles Anywhere
    auto roles_anywhere_signer =
        std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
            absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
            roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());

    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        context_, manager_optref_, "rolesanywhere.ap-southeast-2.amazonaws.com",
        MetadataFetcher::create, "ap-southeast-2", refresh_state, initialization_timer,
        std::move(roles_anywhere_signer), iam_roles_anywhere_config_);
  }

  OptRef<std::shared_ptr<AwsClusterManager>> manager_optref_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  std::shared_ptr<AwsClusterManager> base_manager_;
  envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider iam_roles_anywhere_config_;
};

TEST_F(IamRolesAnywhereCredentialsProviderBadCredentialsTest, InvalidCertsGivesNoCredentials) {

  setupProvider("abc", "def", "ghi");

  auto creds = provider_->getCredentials();
  EXPECT_FALSE(creds.accessKeyId().has_value());
  EXPECT_FALSE(creds.secretAccessKey().has_value());
  EXPECT_FALSE(creds.sessionToken().has_value());
  provider_.reset();
}

TEST_F(IamRolesAnywhereCredentialsProviderBadCredentialsTest, InvalidPrivateKeyGivesNoCredentials) {

  setupProvider(server_root_cert_rsa_pem, "abc", server_root_chain_rsa_pem);

  auto creds = provider_->getCredentials();

  EXPECT_FALSE(creds.accessKeyId().has_value());
  EXPECT_FALSE(creds.secretAccessKey().has_value());
  EXPECT_FALSE(creds.sessionToken().has_value());
  provider_.reset();
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
