#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/protocol.h"

#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/formatter/header_formatter.h"
#include "common/router/string_accessor_impl.h"
#include "common/stream_info/filter_state_impl.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

class StreamInfoHeaderFormatterTest : public testing::Test {
public:
  void testFormatting(const Envoy::StreamInfo::MockStreamInfo& stream_info,
                      const std::string& variable, const std::string& expected_output) {
    {
      auto f = StreamInfoHeaderFormatter(variable, false);
      const std::string formatted_string = f.format(stream_info);
      EXPECT_EQ(expected_output, formatted_string);
    }
  }

  void testFormatting(const std::string& variable, const std::string& expected_output) {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    testFormatting(stream_info, variable, expected_output);
  }

  void testInvalidFormat(const std::string& variable) {
    EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter(variable, false), EnvoyException,
                              fmt::format("field '{}' not supported as custom header", variable));
  }
};

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressVariable) {
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS", "127.0.0.1:0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "127.0.0.1");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS", "127.0.0.2:0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalPortVariable) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for IPv4 address
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
  EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "8443");

  // Validate for IPv6 address
  address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
  EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "9443");

  // Validate for Pipe
  address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
  EXPECT_CALL(stream_info, downstreamLocalAddress()).WillRepeatedly(ReturnRef(address));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "127.0.0.2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamRemoteAddressVariable) {
  testFormatting("UPSTREAM_REMOTE_ADDRESS", "10.0.0.1:443");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.host_.reset();
  testFormatting(stream_info, "UPSTREAM_REMOTE_ADDRESS", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithHostnameVariable) {
  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char*, size_t) -> Api::SysCallIntResult {
          return {-1, ENAMETOOLONG};
        }));
    testFormatting("HOSTNAME", "-");
  }

  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char* name, size_t) -> Api::SysCallIntResult {
          StringUtil::strlcpy(name, "myhostname", 11);
          return {0, 0};
        }));
    testFormatting("HOSTNAME", "myhostname");
  }
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  testFormatting(stream_info, "PROTOCOL", "HTTP/1.1");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanVariableSingleSan) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san"};
  ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(sans));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", "san");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanVariableMultipleSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san1", "san2"};
  ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(sans));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", "san1,san2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, uriSanPeerCertificate())
      .WillByDefault(Return(std::vector<std::string>()));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableSingleSan) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san"};
  ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(sans));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", "san");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableMultipleSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san1", "san2"};
  ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(sans));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", "san1,san2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableNoSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, uriSanLocalCertificate())
      .WillByDefault(Return(std::vector<std::string>()));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubject) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject = "subject";
  ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(subject));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", "subject");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubjectEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject;
  ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(subject));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubjectNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionId) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string session_id = "deadbeef";
  ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(session_id));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", "deadbeef");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionIdEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string session_id;
  ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(session_id));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionIdNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipher) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, ciphersuiteString())
      .WillByDefault(Return("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipherEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, ciphersuiteString()).WillByDefault(Return(""));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipherNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersion) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string tls_version = "TLSv1.2";
  ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tls_version));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", "TLSv1.2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersionEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersionNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerFingerprint) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
  ON_CALL(*connection_info, sha256PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256",
                 "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerFingerprintEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha;
  ON_CALL(*connection_info, sha256PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerFingerprintNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerial) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string serial_number = "b8b5ecc898f2124a";
  ON_CALL(*connection_info, serialNumberPeerCertificate()).WillByDefault(ReturnRef(serial_number));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", "b8b5ecc898f2124a");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerialEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string serial_number;
  ON_CALL(*connection_info, serialNumberPeerCertificate()).WillByDefault(ReturnRef(serial_number));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerialNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuer) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string issuer_peer =
      "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
  ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(issuer_peer));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER",
                 "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuerEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string issuer_peer;
  ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(issuer_peer));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuerNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubject) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string subject_peer =
      "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
  ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(subject_peer));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT",
                 "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubjectEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string subject_peer;
  ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(subject_peer));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubjectNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCert) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_cert = "<some cert>";
  ON_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillByDefault(ReturnRef(expected_cert));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", expected_cert);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_cert;
  ON_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillByDefault(ReturnRef(expected_cert));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStart) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, validFromPeerCertificate()).WillByDefault(Return(startTime));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", "2018-12-18T01:50:34.000Z");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStartEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, validFromPeerCertificate()).WillByDefault(Return(absl::nullopt));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStartNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEnd) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, expirationPeerCertificate()).WillByDefault(Return(startTime));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", "2020-12-17T01:50:34.000Z");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEndEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, expirationPeerCertificate()).WillByDefault(Return(absl::nullopt));
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEndNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(nullptr));
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariable) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            key: value
            nested:
              str_key: str_value
              "escaped,key": escaped_key_value
              bool_key1: true
              bool_key2: false
              num_key1: 1
              num_key2: 3.14
              null_key: null
              list_key: [ list_element ]
              struct_key:
                deep_key: deep_value
      )EOF"));

  // Prove we're testing the expected types.
  const auto& nested_struct =
      Envoy::Config::Metadata::metadataValue(metadata.get(), "namespace", "nested").struct_value();
  EXPECT_EQ(nested_struct.fields().at("str_key").kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key1").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key2").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("null_key").kind_case(), ProtobufWkt::Value::kNullValue);
  EXPECT_EQ(nested_struct.fields().at("list_key").kind_case(), ProtobufWkt::Value::kListValue);
  EXPECT_EQ(nested_struct.fields().at("struct_key").kind_case(), ProtobufWkt::Value::kStructValue);

  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  // Top-level value.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "value");

  // Nested string value.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"str_key\"])",
                 "str_value");

  // Boolean values.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key1\"])",
                 "true");
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key2\"])",
                 "false");

  // Number values.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key1\"])", "1");
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key2\"])",
                 "3.14");

  // Deeply nested value.
  testFormatting(stream_info,
                 "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\", \"deep_key\"])",
                 "deep_value");

  // Initial metadata lookup fails.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"wrong_namespace\", \"key\"])", "");
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"not_found\"])", "");
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"not_found\", \"key\"])", "");

  // Nested metadata lookup fails.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"not_found\"])", "");

  // Nested metadata lookup returns non-struct intermediate value.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"key\", \"invalid\"])", "");

  // Struct values are not rendered.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\"])", "");

  // List values are not rendered.
  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"list_key\"])", "");
}

// Replaces the test of user-defined-headers acting as a Query of Death with
// size checks on user defined headers.
TEST_F(StreamInfoHeaderFormatterTest, ValidateLimitsOnUserDefinedHeaders) {
  {
    envoy::config::route::v3::RouteConfiguration route;
    envoy::config::core::v3::HeaderValueOption* header =
        route.mutable_request_headers_to_add()->Add();
    std::string long_string(16385, 'a');
    header->mutable_header()->set_key("header_name");
    header->mutable_header()->set_value(long_string);
    header->mutable_append()->set_value(true);
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(route), ProtoValidationException,
                            "Proto constraint validation failed.*");
  }
  {
    envoy::config::route::v3::RouteConfiguration route;
    for (int i = 0; i < 1001; ++i) {
      envoy::config::core::v3::HeaderValueOption* header =
          route.mutable_request_headers_to_add()->Add();
      header->mutable_header()->set_key("header_name");
      header->mutable_header()->set_value("value");
    }
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(route), ProtoValidationException,
                            "Proto constraint validation failed.*");
  }
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariableMissingHost) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));

  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithPerRequestStateVariable) {
  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("test_value", filter_state->getDataReadOnly<StringAccessor>("testing").asString());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  testFormatting(stream_info, "PER_REQUEST_STATE(testing)", "test_value");
  testFormatting(stream_info, "PER_REQUEST_STATE(testing2)", "");
  EXPECT_EQ("test_value", filter_state->getDataReadOnly<StringAccessor>("testing").asString());
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithNonStringPerRequestStateVariable) {
  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StreamInfo::TestIntAccessor>(1),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(1, filter_state->getDataReadOnly<StreamInfo::TestIntAccessor>("testing").access());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  testFormatting(stream_info, "PER_REQUEST_STATE(testing)", "");
}

TEST_F(StreamInfoHeaderFormatterTest, WrongFormatOnPerRequestStateVariable) {
  // No parameters
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE()", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE()");

  // Missing single parens
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE(testing", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE(testing");
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE testing)", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE testing)");
}

TEST_F(StreamInfoHeaderFormatterTest, UnknownVariable) { testInvalidFormat("INVALID_VARIABLE"); }

TEST_F(StreamInfoHeaderFormatterTest, WrongFormatOnUpstreamMetadataVariable) {
  // Invalid JSON.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA(abcd)", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(abcd), because JSON supplied is not valid. "
                            "Error(offset 0, line 1): Invalid value.\n");

  // No parameters.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA");

  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA()", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(), because JSON supplied is not valid. "
                            "Error(offset 0, line 1): The document is empty.\n");

  // One parameter.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"ns\"])", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"ns\"])");

  // Missing close paren.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA(", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(");

  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([a,b,c,d]", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([a,b,c,d]");

  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\",\"b\"]", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"a\",\"b\"]");

  // Non-string elements.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", 1])", false), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", 1]), because JSON field from line 1 accessed with type 'String' "
      "does not match actual type 'Integer'.");

  // Invalid string elements.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", \"\\unothex\"])", false), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", \"\\unothex\"]), because JSON supplied is not valid. "
      "Error(offset 7, line 1): Incorrect hex digit after \\u escape in string.\n");

  // Non-array parameters.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA({\"a\":1})", false), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA({\"a\":1}), because JSON field from line 1 accessed with type 'Array' "
      "does not match actual type 'Object'.");
}

} // namespace
} // namespace Router
} // namespace Envoy
