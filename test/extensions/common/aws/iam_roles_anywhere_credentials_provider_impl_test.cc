#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::Eq;
// using testing::InSequence;
using testing::InvokeWithoutArgs;
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

class IamRolesAnywhereCredentialsProviderTest : public testing::Test {
public:
  IamRolesAnywhereCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), raw_metadata_fetcher_(new MockMetadataFetcher) {
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }
  ~IamRolesAnywhereCredentialsProviderTest() override = default;

  void setupProvider(std::string cert, std::string pkey, std::string chain = "") {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));

    EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_ = target.createHandle("test");
    }));

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

    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        *api_, context_,
        [this](Upstream::ClusterManager&, absl::string_view) {
          metadata_fetcher_.reset(raw_metadata_fetcher_);
          return std::move(metadata_fetcher_);
        },
        MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh, std::chrono::seconds(2),
        "arn:role-arn", "arn:profile-arn", "arn:trust-anchor-arn", "session", 3600,
        "ap-southeast-2", "rolesanywhere.ap-southeast-2.amazonaws.com", certificate_data_source_,
        private_key_data_source_, cert_chain_data_source_);
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
                      Http::TestRequestHeaderMapImpl headers) {

    EXPECT_CALL(*raw_metadata_fetcher_, fetch(messageMatches(headers), _, _))
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
      {":scheme", "http"},
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
      {":scheme", "http"},
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
      {":scheme", "http"},
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
  Init::TargetHandlePtr init_target_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
};

// Test cases created from python implementation of iam roles anywhere session
TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigning) {
  // This is what we expect to see requested by the signer
  expectDocument(201, "", rsa_headers_nochain_);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "");
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningInvalidChainOk) {

  expectDocument(201, "", rsa_headers_nochain_);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem, "abc");
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, StandardRSASigningWithChain) {

  expectDocument(201, "", rsa_headers_chain_);

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);

  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(2)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();

  auto creds = provider_->getCredentials();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, CredentialExpiration) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

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
                 rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::minutes(10)), nullptr))
      .Times(2);

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.advanceTimeWait(std::chrono::hours(2));

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
                 rsa_headers_chain_fast_forward_);
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
                 rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(std::chrono::seconds(3595)), nullptr));

  // Kick off a refresh
  timer_->invokeCallback();
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadJsonResponse) {

  // InSequence sequence;
  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  expectDocument(201, R"EOF(
{
 invalidJson
}
 )EOF",
                 rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadCredentialSetValue) {

  // InSequence sequence;
  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  expectDocument(201, R"EOF(
{
  "credentialSet": 1
}
 )EOF",
                 rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, BadCredentialSetArray) {

  // InSequence sequence;
  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  expectDocument(201, R"EOF(
{
  "credentialSet": [
    {
      "invalid":1
    }
  ]
}
 )EOF",
                 rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(IamRolesAnywhereCredentialsProviderTest, EmptyJsonResponse) {

  // InSequence sequence;
  time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));

  expectDocument(201, "", rsa_headers_chain_);

  setupProvider(server_root_cert_rsa_pem, server_root_private_key_rsa_pem,
                server_root_chain_rsa_pem);
  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  init_target_->initialize(init_watcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  // Kick off a refresh
  timer_->invokeCallback();

  const auto credentials = provider_->getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
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

    provider_ = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
        *api_, context_, MetadataFetcher::create,
        MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh, std::chrono::seconds(2),
        "arn:role-arn", "arn:profile-arn", "arn:trust-anchor-arn", "session", 3600,
        "ap-southeast-2", "rolesanywhere.ap-southeast-2.amazonaws.com", certificate_data_source_,
        private_key_data_source_, cert_chain_data_source_);
  }
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
