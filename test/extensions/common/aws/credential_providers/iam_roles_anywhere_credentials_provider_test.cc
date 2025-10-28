#include <cstddef>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

// Test cases created from python implementation of iam roles anywhere session
// Please see iam_roles_anywhere_test_generator.py in this directory to replicate these test cases

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::Eq;
using testing::InvokeWithoutArgs;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

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
      const std::string body = expected_message_.bodyAsString();
      if (body != message.bodyAsString() && !body.empty()) {
        equal = 0;
        *result_listener << "\n"
                         << TestUtility::addLeftAndRightPadding("Expected message body:") << "\n"
                         << body
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
                     std::string session = "session", uint16_t duration = 3600,
                     bool override_cluster = false) {
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

    DataSourceOptRef cert_chain_opt;

    if (chain != "") {
      auto chain_env = std::string("CHAIN");
      TestEnvironment::setEnvVar(chain_env, chain, 1);
      yaml = fmt::format(R"EOF(
      environment_variable: "{}"
    )EOF",
                         chain_env);

      TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source_);

      iam_roles_anywhere_config_.mutable_certificate_chain()->set_environment_variable("CHAIN");
      cert_chain_opt = makeOptRef(iam_roles_anywhere_config_.certificate_chain());
    }

    iam_roles_anywhere_config_.mutable_private_key()->set_environment_variable("PKEY");
    iam_roles_anywhere_config_.mutable_certificate()->set_environment_variable("CERT");
    iam_roles_anywhere_config_.set_role_session_name(session);
    iam_roles_anywhere_config_.mutable_session_duration()->set_seconds(duration);
    iam_roles_anywhere_config_.set_role_arn("arn:role-arn");
    iam_roles_anywhere_config_.set_profile_arn("arn:profile-arn");
    iam_roles_anywhere_config_.set_trust_anchor_arn("arn:trust-anchor-arn");
    mock_manager_ = std::make_shared<MockAwsClusterManager>();
    absl::StatusOr<std::string> return_val = absl::InvalidArgumentError("error");
    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return(
            override_cluster ? return_val : "rolesanywhere.ap-southeast-2.amazonaws.com:443"));
    const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
    const auto initialization_timer = std::chrono::seconds(2);

    auto roles_anywhere_certificate_provider =
        std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
            context_, iam_roles_anywhere_config_.certificate(),
            iam_roles_anywhere_config_.private_key(), cert_chain_opt);
    EXPECT_EQ(roles_anywhere_certificate_provider->initialize(), absl::OkStatus());
    // Create our own x509 signer just for IAM Roles Anywhere
    auto roles_anywhere_signer =
        std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
            absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
            roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());
    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        context_, mock_manager_, "rolesanywhere.ap-southeast-2.amazonaws.com",
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        "ap-southeast-2", refresh_state, initialization_timer, std::move(roles_anywhere_signer),
        iam_roles_anywhere_config_);
    EXPECT_EQ(provider_->providerName(), "IAMRolesAnywhereCredentialsProvider");
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
       "94cee97ea560e0c66774332af8d637728d4ec92fe95f1f0489e37e001cc96d59204ceece0727bee28270f117a16"
       "97e004b68523817e66a2be724be43f5d4daa7701c2d3a253888ff955f804628a4b7857e1d82233d9623a5472988"
       "f8dbedd4dc0013bde4706117bedfe36ea03464949de9fd8cd6ec7105aa41f3b28723c61ffa6b6d9bcb448368df9"
       "04e8bb470066072308a1b773b6a07adc0deb2d941ecfdfef3fb4707900ffe582561424eacdd9c8e1e8c2beed612"
       "33522c339c6efad913797ff5ee39c4e3d43dfe0d8d09afbfbc5a6aefa3ea2264e77315d74861cc01451c3f11f48"
       "adbf9c25ac451841c9770ed6e08a862537bb30d8d04fd6da46c426195"},
      {"x-amz-date", "20180102T030405Z"},
      {"x-amz-content-sha256", "9459c733184fbb190c1e0be65b079c66e4bf96b6edddcb6c39a1a939460e6d55"},
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
      {"x-amz-content-sha256", "9459c733184fbb190c1e0be65b079c66e4bf96b6edddcb6c39a1a939460e6d55"},
      {"authorization",
       "AWS4-X509-RSA-SHA256 "
       "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
       "aws4_request, "
       "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509;x-amz-x509-"
       "chain, "
       "Signature="
       "8c5b5daa03ab9ac9513e2241eeb31a78215f4b2c8336122ad0c6c3962b625a64f0133da911ecf2e1868d402ce9b"
       "dfc5015cc9446b8be4d3dc2fdc3bcda826bb1f333a77e53632ec0d4dfe18b88b31207c8a9547fc5f22d77707196"
       "326d3cc7568d40d8c1ab929559f4d8300925312efc51bad5739254140307ee071379e2c159c015df501f5941829"
       "575ad0a3b41d8e04d02beec8bea615e21bef1a91f5e4a97d0b2f55b737fca32b790ecb7c87d9519e947c80f4279"
       "f6c57965c04fa040959c9449fa64da1ac0c193c601b6a312d3b8eb0c0f26417a6c9b63c6d8e9665ba00cbb5ea0d"
       "116956516cb8da33bddc5a665a9b4f590a00df9c611cb15cfc4ed1471"}};

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
      {"x-amz-content-sha256", "9459c733184fbb190c1e0be65b079c66e4bf96b6edddcb6c39a1a939460e6d55"},
      {"authorization",
       "AWS4-X509-RSA-SHA256 "
       "Credential=131827979019394590882466519576505238184/20180102/ap-southeast-2/rolesanywhere/"
       "aws4_request, "
       "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;x-amz-x509;x-amz-x509-"
       "chain, "
       "Signature="
       "0a546bb10a5169c30f53788b38fade69e9af8eb20debc8f3fc33ec2123c0406fcf831728109920e2a34f094c353"
       "7b82f10c97a6159c53190b7b4cb65899bbe8db20bd1f5eac3412d40c25b4d08b891707efe7d82d1ac529957cf02"
       "eaebee6f2ba0b7937b18922995d2fd24d7a58a83a07fb6d1f8233ee853d0b11c0ae7ef7b4ae6374ba232da93f8e"
       "62027378af712e4a9f5035df5b6d0d1b596d84d955caf1a50ec8c276abf163c3b00df92f9722aea041e2437f369"
       "56b05aa487941592921cf4327623afb5679e83b94b03c7b0e2511624193f5b621148516e316ef54714aa8dbaa8b"
       "ababdc5e88c82c20eee3cdf0be48ff0eaf8217fb16d90f35793d0f1b0"}};

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
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider iam_roles_anywhere_config_;
};

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

TEST_F(IamRolesAnywhereCredentialsProviderTest, BrokenClusterManager) {

  // This is what we expect to see requested by the signer
  auto headers =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_nochain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "", "session", 3600,
                "testcluster.xxx");

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
  EXPECT_EQ(creds.hasCredentials(), false);
  delete (raw_metadata_fetcher_);
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
      "949b52d936e142f1f2b62fedb05cb7733b5ef3a3ae070f6b342d2a6173e2a712e7586fc00688974f0562fa09d917"
      "07615aec1e6e3174513d421b1b9093820bbd65a5e2bd99621118a8a9cf01fb5a0cc7921e69d7f9e41117e43f60b8"
      "36d17f5d7b7197e3039069a73cfa26021c3a8ee2fa10d30582cdfda1f371a88ff951bf99c9057bc45bcd850d579a"
      "d1053f9d6aebe190d198177fae03ef06be32b6ea1c57ef28c17ea0aa16ced09ebb6529e53fe6ec8ca68089da1374"
      "9c2d076ff2a5a499672fccba4ef3d6ad606c217c553708542f89b9e6f4991fcb2ca5d1cd966d0c1377f4dfe4fadf"
      "2c883b43fa1b2f78a25a890dfc2c3f5699e3c1290b7514de390b");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "26ba3087303676735d4b0a63543d5ef4e74be47839e11b5c283d707ddbec6deb");

  message.body().add(
      "{\"durationSeconds\":3600.0,\"profileArn\":\"arn:profile-arn\",\"roleArn\":\"arn:role-arn\","
      "\"roleSessionName\":\"mysession\",\"trustAnchorArn\":\"arn:trust-anchor-arn\"}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "", "mysession");

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
      "a5c4ab3c911805ce0dc74f90855fc958cab2b0c1104a85e04140d878dd7982a0cf6dcbf2ba92a8530f64c5821a9c"
      "40b3e131cd4da3c26b14535345119e874e79dad118a0eeb0161b92c96ad48cd5b252046c742164c31c87ac249b9f"
      "9673887d3f60cfbf9e1499d461fc7a6d8bd0b182e70ed44e56e5a29362f93ed3e3b6ebc2f7df72e9e16957e4117f"
      "f3dff8f44674bfabf0c39f338ffa7e45ada6de33722a3c1021832e6f04847297d350c8eed700ee4efeacd6ac91fc"
      "4df2edb198304a1f1e87eca3946b23d4d3e319c39c368f958766b1101596a36024ce15fc2aa2200e50f65cfbed64"
      "77597591a9afb444876c62cdede451632097b5e370db7a1e05e1");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "265fe6c14e7b03446a0b273bbdfaef3a0a9e83810298706c0e2bd4b30d899338");

  message.body().add("{\"durationSeconds\":3600.0,\"profileArn\":\"arn:profile-arn\",\"roleArn\":"
                     "\"arn:role-arn\",\"trustAnchorArn\":\"arn:trust-anchor-arn\"}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "", "");

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
      "8a1bdc11741fbd2ea3da7626d1cebb78b104813f842adfa18d099631f6354758d4863a04dfb55487b71df1d4fc74"
      "dea3d3232f9f3f582880da8296666e22272a808f57d71b39866271622c0e65c1e27ac6cc8222f6cdbbdc6f0071b0"
      "1dd610ffab95f99f9d41fbbac347536d6113df900ce795d783c3486e53d9f8b01c8bc71ea49095aafa5d3fbd47c6"
      "d6c0e5c4cfe93826f09fbce8e54a0e095dc018ba96f9c12e073e54b87d26457a953a5276e554f9e3285494891aee"
      "54a4ce7e5d76dbe57bcf228b3a22fc7e697403abccce863cc948ab643de15ff97154430f362570ce3afaa77a8d87"
      "17210f88b2965d2acf8098321434367e5c285ed5766bbec9120e");
  message.headers().setCopy(Http::LowerCaseString("x-amz-content-sha256"),
                            "d423ab05be9a4ea24346dd8c9beccd7ce30b10322d9b2bab54a75303b4201889");

  message.body().add("{\"durationSeconds\":123.0,\"profileArn\":\"arn:profile-arn\",\"roleArn\":"
                     "\"arn:role-arn\",\"roleSessionName\":\"mysession\",\"trustAnchorArn\":\"arn:"
                     "trust-anchor-arn\""
                     "}");

  expectDocument(201, "", message);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "", "mysession", 123);

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
  // 10 minutes - 60s grace period = 540 seconds
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::minutes(10)) -
                                       std::chrono::milliseconds(std::chrono::seconds(60)),
                                   nullptr))
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
  EXPECT_CALL(*raw_metadata_fetcher_, cancel()).Times(2);
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
  // 1 hour - 60s grace period = 3540 seconds
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3540)), nullptr));

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
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
      .WillRepeatedly(Return("rolesanywhere.ap-southeast-2.amazonaws.com:443"));

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);
  auto chain_optref = makeOptRef(iam_roles_anywhere_config_.certificate_chain());

  auto roles_anywhere_certificate_provider =
      std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
          context_, iam_roles_anywhere_config_.certificate(),
          // iam_roles_anywhere_config_.private_key(),
          // makeOptRef(iam_roles_anywhere_config_.certificate_chain()));
          iam_roles_anywhere_config_.private_key(), chain_optref);

  std::unique_ptr<MockIAMRolesAnywhereSigV4Signer> mock_signer =
      std::make_unique<MockIAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());
  auto* mock_ptr = mock_signer.get();

  // Force signing to fail
  EXPECT_CALL(*mock_ptr, sign(_, true, _)).WillOnce(Return(absl::InvalidArgumentError("error")));

  provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
      context_, mock_manager_, "rolesanywhere.ap-southeast-2.amazonaws.com",
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
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, SessionsApi4xx) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));
  expectDocument(403, "", message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
  EXPECT_FALSE(creds.accessKeyId().has_value());
  EXPECT_FALSE(creds.secretAccessKey().has_value());
  EXPECT_FALSE(creds.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, TestCancel) {
  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));

  expectDocument(200, std::move(R"EOF(
not json
)EOF"),
                 message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  auto mock_fetcher = std::make_unique<MockMetadataFetcher>();

  EXPECT_CALL(*mock_fetcher, cancel).Times(2);
  EXPECT_CALL(*mock_fetcher, fetch(_, _, _));
  // Ensure we have a metadata fetcher configured, so we expect this to receive a cancel
  provider_friend.setMetadataFetcher(std::move(mock_fetcher));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
  delete (raw_metadata_fetcher_);
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, SessionsApi5xx) {

  // Setup timer.
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{rsa_headers_chain_}};
  Http::RequestMessageImpl message(std::move(headers));
  expectDocument(503, "", message);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
  EXPECT_FALSE(creds.accessKeyId().has_value());
  EXPECT_FALSE(creds.secretAccessKey().has_value());
  EXPECT_FALSE(creds.sessionToken().has_value());
}

class IamRolesAnywhereCredentialsProviderBadCredentialsTest : public testing::Test {
public:
  IamRolesAnywhereCredentialsProviderBadCredentialsTest() : api_(Api::createApiForTest()) {};

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

    EXPECT_CALL(*mock_manager_, getUriFromClusterName(_))
        .WillRepeatedly(Return("rolesanywhere.ap-southeast-2.amazonaws.com/sessions"));

    const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
    const auto initialization_timer = std::chrono::seconds(2);
    auto chain_optref = makeOptRef(iam_roles_anywhere_config_.certificate_chain());

    auto roles_anywhere_certificate_provider =
        std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
            context_, iam_roles_anywhere_config_.certificate(),
            iam_roles_anywhere_config_.private_key(), chain_optref);
    // Create our own x509 signer just for IAM Roles Anywhere
    auto roles_anywhere_signer =
        std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
            absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
            roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());

    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        context_, mock_manager_, "rolesanywhere.ap-southeast-2.amazonaws.com",
        MetadataFetcher::create, "ap-southeast-2", refresh_state, initialization_timer,
        std::move(roles_anywhere_signer), iam_roles_anywhere_config_);
  }

  std::shared_ptr<MockAwsClusterManager> mock_manager_;
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

class IamRolesAnywhereCredentialsProviderBasicTests : public testing::Test {
public:
  IamRolesAnywhereCredentialsProviderBasicTests() = default;
  ~IamRolesAnywhereCredentialsProviderBasicTests() override = default;

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests, SignEmptyPayload) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

  auto mock_credentials_provider = std::make_shared<MockX509CredentialsProvider>();

  X509Credentials creds =
      X509Credentials("cert", X509Credentials::PublicKeySignatureAlgorithm::RSA, "serial", "chain",
                      "pem", context_.timeSystem().systemTime());

  EXPECT_CALL(*mock_credentials_provider, getCredentials()).WillRepeatedly(Return(creds));
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          mock_credentials_provider, context_.mainThreadDispatcher().timeSource());

  Http::TestRequestHeaderMapImpl headers{};
  absl::Status status;
  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");
  status = roles_anywhere_signer->signEmptyPayload(headers, "ap-southeast-2");
  // Will fail because credentials are invalid
  EXPECT_FALSE(status.ok());
}

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests, SignUnsignedPayload) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

  auto mock_credentials_provider = std::make_shared<MockX509CredentialsProvider>();
  X509Credentials creds =
      X509Credentials("cert", X509Credentials::PublicKeySignatureAlgorithm::RSA, "serial", "chain",
                      "pem", context_.timeSystem().systemTime());

  EXPECT_CALL(*mock_credentials_provider, getCredentials()).WillRepeatedly(Return(creds));
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          mock_credentials_provider, context_.mainThreadDispatcher().timeSource());

  Http::TestRequestHeaderMapImpl headers{};
  absl::Status status;
  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");
  status = roles_anywhere_signer->signUnsignedPayload(headers, "ap-southeast-2");
  // Will fail because credentials are invalid
  EXPECT_FALSE(status.ok());
  mock_credentials_provider.reset();
}

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests, NoMethod) {
  auto mock_credentials_provider = std::make_shared<MockX509CredentialsProvider>();

  X509Credentials creds =
      X509Credentials("cert", X509Credentials::PublicKeySignatureAlgorithm::RSA, "serial", "chain",
                      "pem", context_.timeSystem().systemTime());

  EXPECT_CALL(*mock_credentials_provider, getCredentials()).WillRepeatedly(Return(creds));
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          mock_credentials_provider, context_.mainThreadDispatcher().timeSource());

  Http::TestRequestHeaderMapImpl headers{};
  absl::Status status;
  // No Method
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");
  status = roles_anywhere_signer->signUnsignedPayload(headers, "ap-southeast-2");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Message is missing :method header");
}

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests, NoPath) {
  auto mock_credentials_provider = std::make_shared<MockX509CredentialsProvider>();

  X509Credentials creds =
      X509Credentials("cert", X509Credentials::PublicKeySignatureAlgorithm::RSA, "serial", "chain",
                      "pem", context_.timeSystem().systemTime());

  EXPECT_CALL(*mock_credentials_provider, getCredentials()).WillRepeatedly(Return(creds));
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          mock_credentials_provider, context_.mainThreadDispatcher().timeSource());

  Http::TestRequestHeaderMapImpl headers{};
  absl::Status status;
  // No Path
  headers.setMethod("GET");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");
  status = roles_anywhere_signer->signUnsignedPayload(headers, "ap-southeast-2");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Message is missing :path header");
}

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests, NoCredentials) {
  auto roles_anywhere_certificate_provider = std::make_shared<MockX509CredentialsProvider>();

  // Blank credentials set here
  EXPECT_CALL(*roles_anywhere_certificate_provider, getCredentials())
      .WillRepeatedly(Return(X509Credentials()));

  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());

  Http::TestRequestHeaderMapImpl headers{};
  absl::Status status;
  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");
  status = roles_anywhere_signer->signEmptyPayload(headers, "ap-southeast-2");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "Unable to sign IAM Roles Anywhere payload - no x509 credentials found");
}

class ControlledCredentialsProvider : public CredentialsProvider {
public:
  ControlledCredentialsProvider(CredentialSubscriberCallbacks* cb) : cb_(cb) {}

  Credentials getCredentials() override {
    Thread::LockGuard guard(mu_);
    return credentials_;
  }

  bool credentialsPending() override {
    Thread::LockGuard guard(mu_);
    return pending_;
  }

  std::string providerName() override { return "Controlled Credentials Provider"; }

  void refresh(const Credentials& credentials) {
    {
      Thread::LockGuard guard(mu_);
      credentials_ = credentials;
      pending_ = false;
    }
    if (cb_) {
      cb_->onCredentialUpdate();
    }
  }

private:
  CredentialSubscriberCallbacks* cb_;
  Thread::MutexBasicLockable mu_;
  Credentials credentials_ ABSL_GUARDED_BY(mu_);
  bool pending_ ABSL_GUARDED_BY(mu_) = true;
};

TEST_F(IamRolesAnywhereCredentialsProviderBasicTests,
       SignerCallbacksCalledWhenCredentialsReturned) {
  MockFunction<void()> signer_callback;
  EXPECT_CALL(signer_callback, Call());

  auto roles_anywhere_certificate_provider = std::make_shared<MockX509CredentialsProvider>();

  // Blank credentials set here
  EXPECT_CALL(*roles_anywhere_certificate_provider, getCredentials())
      .WillRepeatedly(Return(X509Credentials()));

  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view("ap-southeast-2"),
          roles_anywhere_certificate_provider, context_.mainThreadDispatcher().timeSource());

  CredentialsProviderChain chain;

  auto provider = std::make_shared<ControlledCredentialsProvider>(&chain);
  chain.add(provider);
  ASSERT_TRUE(chain.addCallbackIfChainCredentialsPending(signer_callback.AsStdFunction()));
  provider->refresh(Credentials());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
