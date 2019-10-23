#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/http/protocol.h"

#include "common/config/metadata.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/router/header_formatter.h"
#include "common/router/header_parser.h"
#include "common/router/string_accessor_impl.h"
#include "common/stream_info/filter_state_impl.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

static envoy::api::v2::route::Route parseRouteFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::route::Route route;
  TestUtility::loadFromYaml(yaml, route);
  return route;
}

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
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "127.0.0.1");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS", "127.0.0.2:0");
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

  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
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
      Envoy::Config::Metadata::metadataValue(*metadata, "namespace", "nested").struct_value();
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
    envoy::api::v2::RouteConfiguration route;
    envoy::api::v2::core::HeaderValueOption* header = route.mutable_request_headers_to_add()->Add();
    std::string long_string(16385, 'a');
    header->mutable_header()->set_key("header_name");
    header->mutable_header()->set_value(long_string);
    header->mutable_append()->set_value(true);
    EXPECT_THROW_WITH_REGEX(TestUtility::validate(route), ProtoValidationException,
                            "Proto constraint validation failed.*");
  }
  {
    envoy::api::v2::RouteConfiguration route;
    for (int i = 0; i < 1001; ++i) {
      envoy::api::v2::core::HeaderValueOption* header =
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
  Envoy::StreamInfo::FilterStateImpl filter_state;
  filter_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                       StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_EQ("test_value", filter_state.getDataReadOnly<StringAccessor>("testing").asString());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(filter_state));

  testFormatting(stream_info, "PER_REQUEST_STATE(testing)", "test_value");
  testFormatting(stream_info, "PER_REQUEST_STATE(testing2)", "");
  EXPECT_EQ("test_value", filter_state.getDataReadOnly<StringAccessor>("testing").asString());
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithNonStringPerRequestStateVariable) {
  Envoy::StreamInfo::FilterStateImpl filter_state;
  filter_state.setData("testing", std::make_unique<StreamInfo::TestIntAccessor>(1),
                       StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_EQ(1, filter_state.getDataReadOnly<StreamInfo::TestIntAccessor>("testing").access());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(filter_state));

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

TEST(HeaderParserTest, TestParseInternal) {
  struct TestCase {
    std::string input_;
    absl::optional<std::string> expected_output_;
    absl::optional<std::string> expected_exception_;
  };

  static const TestCase test_cases[] = {
      // Valid inputs
      {"", {}, {}},
      {"%PROTOCOL%", {"HTTP/1.1"}, {}},
      {"[%PROTOCOL%", {"[HTTP/1.1"}, {}},
      {"%PROTOCOL%]", {"HTTP/1.1]"}, {}},
      {"[%PROTOCOL%]", {"[HTTP/1.1]"}, {}},
      {"%%%PROTOCOL%", {"%HTTP/1.1"}, {}},
      {"%PROTOCOL%%%", {"HTTP/1.1%"}, {}},
      {"%%%PROTOCOL%%%", {"%HTTP/1.1%"}, {}},
      {"%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.1"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS%", {"127.0.0.2:0"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%", {"127.0.0.2"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"value"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"[value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"value]"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"[value]"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \t \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \n \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA( \t [ \t \"ns\" \t , \t \"key\" \t ] \t )%", {"value"}, {}},
      {R"EOF(%UPSTREAM_METADATA(["\"quoted\"", "\"key\""])%)EOF", {"value"}, {}},
      {"%UPSTREAM_REMOTE_ADDRESS%", {"10.0.0.1:443"}, {}},
      {"%PER_REQUEST_STATE(testing)%", {"test_value"}, {}},
      {"%START_TIME%", {"2018-04-03T23:06:09.123Z"}, {}},

      // Unescaped %
      {"%", {}, {"Invalid header configuration. Un-escaped % at position 0"}},
      {"before %", {}, {"Invalid header configuration. Un-escaped % at position 7"}},
      {"%% infix %", {}, {"Invalid header configuration. Un-escaped % at position 9"}},

      // Unknown variable names
      {"%INVALID%", {}, {"field 'INVALID' not supported as custom header"}},
      {"before %INVALID%", {}, {"field 'INVALID' not supported as custom header"}},
      {"%INVALID% after", {}, {"field 'INVALID' not supported as custom header"}},
      {"before %INVALID% after", {}, {"field 'INVALID' not supported as custom header"}},

      // Un-terminated variable expressions.
      {"%VAR", {}, {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"%%%VAR", {}, {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %VAR",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %%%VAR",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %VAR after",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR after'"}},
      {"before %%%VAR after",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR after'"}},
      {"% ", {}, {"Invalid header configuration. Un-terminated variable expression ' '"}},

      // Parsing errors in variable expressions that take a JSON-array parameter.
      {"%UPSTREAM_METADATA(no array)%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA(no array), because JSON supplied is not valid. "
        "Error(offset 1, line 1): Invalid value.\n"}},
      {"%UPSTREAM_METADATA( no array)%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA( no array), because JSON supplied is not valid. "
        "Error(offset 2, line 1): Invalid value.\n"}},
      {"%UPSTREAM_METADATA([\"unterminated array\")%",
       {},
       {"Invalid header configuration. Expecting ',', ']', or whitespace after "
        "'UPSTREAM_METADATA([\"unterminated array\"', but found ')'"}},
      {"%UPSTREAM_METADATA([not-a-string])%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'UPSTREAM_METADATA([', "
        "but found 'n'"}},
      {"%UPSTREAM_METADATA([\"\\",
       {},
       {"Invalid header configuration. Un-terminated backslash in JSON string after "
        "'UPSTREAM_METADATA([\"'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"]x",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"]', but found 'x'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])x",
       {},
       {"Invalid header configuration. Expecting '%' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"])', but found 'x'"}},

      {"%PER_REQUEST_STATE no parens%",
       {},
       {"Invalid header configuration. Expected format PER_REQUEST_STATE(<data_name>), "
        "actual format PER_REQUEST_STATE no parens"}},

      // Invalid arguments
      {"%UPSTREAM_METADATA%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA"}},
      {"%UPSTREAM_METADATA([\"ns\"])%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA([\"ns\"])"}},
      {"%START_TIME(%85n)%", {}, {"Invalid header configuration. Format string contains newline."}},

  };

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));

  // Upstream metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
          R"EOF(
        filter_metadata:
          ns:
            key: value
          '"quoted"':
            '"key"': value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  ON_CALL(stream_info, startTime()).WillByDefault(Return(start_time));

  Envoy::StreamInfo::FilterStateImpl filter_state;
  filter_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                       StreamInfo::FilterState::StateType::ReadOnly);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(filter_state));

  for (const auto& test_case : test_cases) {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> to_add;
    envoy::api::v2::core::HeaderValueOption* header = to_add.Add();
    header->mutable_header()->set_key("x-header");
    header->mutable_header()->set_value(test_case.input_);

    if (test_case.expected_exception_) {
      EXPECT_FALSE(test_case.expected_output_);
      EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(to_add), EnvoyException,
                                test_case.expected_exception_.value());
      continue;
    }

    HeaderParserPtr req_header_parser = HeaderParser::configure(to_add);

    Http::TestHeaderMapImpl header_map{{":method", "POST"}};
    req_header_parser->evaluateHeaders(header_map, stream_info);

    std::string descriptor = fmt::format("for test case input: {}", test_case.input_);

    if (!test_case.expected_output_) {
      EXPECT_FALSE(header_map.has("x-header")) << descriptor;
      continue;
    }

    EXPECT_TRUE(header_map.has("x-header")) << descriptor;
    EXPECT_EQ(test_case.expected_output_.value(), header_map.get_("x-header")) << descriptor;
  }
}

TEST(HeaderParserTest, EvaluateHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
}

TEST(HeaderParserTest, EvaluateEmptyHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-key"
      value: "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>();
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-key"));
}

TEST(HeaderParserTest, EvaluateStaticHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("static-header"));
  EXPECT_EQ("static-value", header_map.get_("static-header"));
}

TEST(HeaderParserTest, EvaluateCompoundHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
request_headers_to_add:
  - header:
      key: "x-prefix"
      value: "prefix-%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-suffix"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%-suffix"
  - header:
      key: "x-both"
      value: "prefix-%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%-suffix"
  - header:
      key: "x-escaping-1"
      value: "%%%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%%%"
  - header:
      key: "x-escaping-2"
      value: "%%%%%%"
  - header:
      key: "x-multi"
      value: "%PROTOCOL% from %DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-multi-back-to-back"
      value: "%PROTOCOL%%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-metadata"
      value: "%UPSTREAM_METADATA([\"namespace\", \"%key%\"])%"
  - header:
      key: "x-per-request"
      value: "%PER_REQUEST_STATE(testing)%"
request_headers_to_remove: ["x-nope"]
  )EOF";

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));

  // Metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            "%key%": value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  Envoy::StreamInfo::FilterStateImpl filter_state;
  filter_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                       StreamInfo::FilterState::StateType::ReadOnly);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(filter_state));

  req_header_parser->evaluateHeaders(header_map, stream_info);

  EXPECT_TRUE(header_map.has("x-prefix"));
  EXPECT_EQ("prefix-127.0.0.1", header_map.get_("x-prefix"));

  EXPECT_TRUE(header_map.has("x-suffix"));
  EXPECT_EQ("127.0.0.1-suffix", header_map.get_("x-suffix"));

  EXPECT_TRUE(header_map.has("x-both"));
  EXPECT_EQ("prefix-127.0.0.1-suffix", header_map.get_("x-both"));

  EXPECT_TRUE(header_map.has("x-escaping-1"));
  EXPECT_EQ("%127.0.0.1%", header_map.get_("x-escaping-1"));

  EXPECT_TRUE(header_map.has("x-escaping-2"));
  EXPECT_EQ("%%%", header_map.get_("x-escaping-2"));

  EXPECT_TRUE(header_map.has("x-multi"));
  EXPECT_EQ("HTTP/1.1 from 127.0.0.1", header_map.get_("x-multi"));

  EXPECT_TRUE(header_map.has("x-multi-back-to-back"));
  EXPECT_EQ("HTTP/1.1127.0.0.1", header_map.get_("x-multi-back-to-back"));

  EXPECT_TRUE(header_map.has("x-metadata"));
  EXPECT_EQ("value", header_map.get_("x-metadata"));

  EXPECT_TRUE(header_map.has("x-per-request"));
  EXPECT_EQ("test_value", header_map.get_("x-per-request"));

  EXPECT_TRUE(header_map.has("x-safe"));
  EXPECT_FALSE(header_map.has("x-nope"));
}

TEST(HeaderParserTest, EvaluateHeadersWithAppendFalse) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append: true
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append: true
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s%3f)%"
    append: true
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append: true
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append: true
)EOF";

  // Disable append mode.
  envoy::api::v2::route::Route route = parseRouteFromV2Yaml(ymal);
  route.mutable_request_headers_to_add(0)->mutable_append()->set_value(false);
  route.mutable_request_headers_to_add(1)->mutable_append()->set_value(false);
  route.mutable_request_headers_to_add(2)->mutable_append()->set_value(false);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route.request_headers_to_add());
  Http::TestHeaderMapImpl header_map{
      {":method", "POST"}, {"static-header", "old-value"}, {"x-client-ip", "0.0.0.0"}};

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(3).WillRepeatedly(Return(start_time));

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("static-header"));
  EXPECT_EQ("static-value", header_map.get_("static-header"));
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_EQ("127.0.0.1", header_map.get_("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-request-start"));
  EXPECT_EQ("1522796769123", header_map.get_("x-request-start"));
  EXPECT_TRUE(header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", header_map.get_("x-request-start-default"));
  EXPECT_TRUE(header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            header_map.get_("x-request-start-range"));

  using CountMap = absl::flat_hash_map<std::string, int>;
  CountMap counts;
  header_map.iterate(
      [](const Http::HeaderEntry& header, void* cb_v) -> Http::HeaderMap::Iterate {
        CountMap* m = static_cast<CountMap*>(cb_v);
        absl::string_view key = header.key().getStringView();
        CountMap::iterator i = m->find(key);
        if (i == m->end()) {
          m->insert({std::string(key), 1});
        } else {
          i->second++;
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &counts);

  EXPECT_EQ(1, counts["static-header"]);
  EXPECT_EQ(1, counts["x-client-ip"]);
  EXPECT_EQ(1, counts["x-request-start"]);
}

TEST(HeaderParserTest, EvaluateResponseHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
response_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append: true
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s.%3f)%"
    append: true
  - header:
      key: "x-request-start-multiple"
      value: "%START_TIME(%s.%3f)% %START_TIME% %START_TIME(%s)%"
    append: true
  - header:
      key: "x-request-start-f"
      value: "%START_TIME(f)%"
    append: true
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append: true
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append: true
  - header:
      key: "set-cookie"
      value: "foo"
  - header:
      key: "set-cookie"
      value: "bar"
    append: true

response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Initialize start_time as 2018-04-03T23:06:09.123Z in microseconds.
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(7).WillRepeatedly(Return(start_time));

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-request-start-multiple"));
  EXPECT_TRUE(header_map.has("x-safe"));
  EXPECT_FALSE(header_map.has("x-nope"));
  EXPECT_TRUE(header_map.has("x-request-start"));
  EXPECT_EQ("1522796769.123", header_map.get_("x-request-start"));
  EXPECT_EQ("1522796769.123 2018-04-03T23:06:09.123Z 1522796769",
            header_map.get_("x-request-start-multiple"));
  EXPECT_TRUE(header_map.has("x-request-start-f"));
  EXPECT_EQ("f", header_map.get_("x-request-start-f"));
  EXPECT_TRUE(header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", header_map.get_("x-request-start-default"));
  EXPECT_TRUE(header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            header_map.get_("x-request-start-range"));
  EXPECT_EQ("foo", header_map.get_("set-cookie"));

  // Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
  // combine set-cookie headers
  std::vector<absl::string_view> out;
  Http::HeaderUtility::getAllOfHeader(header_map, "set-cookie", out);
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0], "foo");
  ASSERT_EQ(out[1], "bar");
}

TEST(HeaderParserTest, EvaluateRequestHeadersRemoveBeforeAdd) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
request_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "bar"
request_headers_to_remove: ["x-foo-header"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

TEST(HeaderParserTest, EvaluateResponseHeadersRemoveBeforeAdd) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "bar"
response_headers_to_remove: ["x-foo-header"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

} // namespace
} // namespace Router
} // namespace Envoy
