#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/protocol.h"

#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/header_formatter.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

static envoy::config::route::v3::Route parseRouteFromV3Yaml(const std::string& yaml) {
  envoy::config::route::v3::Route route;
  TestUtility::loadFromYaml(yaml, route);
  return route;
}

class StreamInfoHeaderFormatterTest : public testing::Test {
public:
  void testFormatting(const Envoy::StreamInfo::MockStreamInfo& stream_info,
                      const std::string& variable, const std::string& expected_output) {
    {
      auto f = StreamInfoHeaderFormatter(variable);
      const std::string formatted_string = f.format(stream_info);
      EXPECT_EQ(expected_output, formatted_string);
    }
    if (test_with_and_without_runtime_) {
      TestScopedRuntime runtime_;
      auto f = StreamInfoHeaderFormatter(variable);
      const std::string formatted_string = f.format(stream_info);
      EXPECT_EQ(expected_output, formatted_string);
    }
  }

  void testFormatting(const std::string& variable, const std::string& expected_output) {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    testFormatting(stream_info, variable, expected_output);
  }

  void testInvalidFormat(const std::string& variable) {
    EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter{variable}, EnvoyException,
                              fmt::format("field '{}' not supported as custom header", variable));
  }
  bool test_with_and_without_runtime_ = false;
};

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressVariable) {
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS", "127.0.0.1:0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "127.0.0.1");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamRemotePortVariable) {
  testFormatting("DOWNSTREAM_REMOTE_PORT", "0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamDirectRemoteAddressVariable) {
  testFormatting("DOWNSTREAM_DIRECT_REMOTE_ADDRESS", "127.0.0.3:63443");
}

TEST_F(StreamInfoHeaderFormatterTest,
       TestFormatWithDownstreamDirectRemoteAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT", "127.0.0.3");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamDirectRemotePortVariable) {
  testFormatting("DOWNSTREAM_DIRECT_REMOTE_PORT", "63443");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS", "127.0.0.2:0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressVariableVariants) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for IPv4 address
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS", "127.1.2.3:8443");

  // Validate for IPv6 address
  address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS", "[::1]:9443");

  // Validate for Pipe
  address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS", "/foo");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalPortVariable) {
  testFormatting("DOWNSTREAM_LOCAL_PORT", "0");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalPortVariableVariants) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for IPv4 address
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "8443");

  // Validate for IPv6 address
  address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "9443");

  // Validate for Pipe
  address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_PORT", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "127.0.0.2");
}

TEST_F(StreamInfoHeaderFormatterTest,
       TestFormatWithDownstreamLocalAddressWithoutPortVariableVariants) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for IPv4 address
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "127.1.2.3");

  // Validate for IPv6 address
  address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "::1");

  // Validate for Pipe
  address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "/foo");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamRemoteAddressVariable) {
  test_with_and_without_runtime_ = true;
  testFormatting("UPSTREAM_REMOTE_ADDRESS", "10.0.0.1:443");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  Network::Address::InstanceConstSharedPtr nullptr_address;
  EXPECT_CALL(*dynamic_cast<StreamInfo::MockUpstreamInfo*>(stream_info.upstream_info_.get()),
              upstreamRemoteAddress())
      .WillRepeatedly(ReturnRef(nullptr_address));
  testFormatting(stream_info, "UPSTREAM_REMOTE_ADDRESS", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamRemotePortVariable) {
  test_with_and_without_runtime_ = true;
  testFormatting("UPSTREAM_REMOTE_PORT", "443");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  Network::Address::InstanceConstSharedPtr nullptr_address;
  EXPECT_CALL(*dynamic_cast<StreamInfo::MockUpstreamInfo*>(stream_info.upstream_info_.get()),
              upstreamRemoteAddress())
      .WillRepeatedly(ReturnRef(nullptr_address));
  testFormatting(stream_info, "UPSTREAM_REMOTE_PORT", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamRemoteAddressWithoutPortVariable) {
  test_with_and_without_runtime_ = true;
  testFormatting("UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "10.0.0.1");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  Network::Address::InstanceConstSharedPtr nullptr_address;
  EXPECT_CALL(*dynamic_cast<StreamInfo::MockUpstreamInfo*>(stream_info.upstream_info_.get()),
              upstreamRemoteAddress())
      .WillRepeatedly(ReturnRef(nullptr_address));
  testFormatting(stream_info, "UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamLocalAddressVariable) {
  testFormatting("UPSTREAM_LOCAL_ADDRESS", "127.1.2.3:58443");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  stream_info.upstreamInfo()->setUpstreamLocalAddress(nullptr);
  testFormatting(stream_info, "UPSTREAM_LOCAL_ADDRESS", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamLocalPortVariable) {
  testFormatting("UPSTREAM_LOCAL_PORT", "58443");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  stream_info.upstreamInfo()->setUpstreamLocalAddress(nullptr);
  testFormatting(stream_info, "UPSTREAM_LOCAL_PORT", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestformatWithUpstreamLocalAddressWithoutPortVariable) {
  testFormatting("UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "127.1.2.3");

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  stream_info.upstreamInfo()->setUpstreamLocalAddress(nullptr);
  testFormatting(stream_info, "UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "");
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

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithRequestedServerNameVariable) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for empty Request Server Name
  testFormatting(stream_info, "REQUESTED_SERVER_NAME", "");

  // Validate for a valid Request Server Name
  const std::string requested_server_name = "foo.bar";
  stream_info.downstream_connection_info_provider_->setRequestedServerName(requested_server_name);
  testFormatting(stream_info, "REQUESTED_SERVER_NAME", requested_server_name);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithVirtualClusterNameVariable) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  // Validate for empty VC
  testFormatting(stream_info, "VIRTUAL_CLUSTER_NAME", "");

  // Validate for a valid VC
  const std::string virtual_cluster_name = "authN";
  stream_info.setVirtualClusterName(virtual_cluster_name);
  testFormatting(stream_info, "VIRTUAL_CLUSTER_NAME", virtual_cluster_name);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanVariableSingleSan) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san"};
  ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(sans));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", "san");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanVariableMultipleSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san1", "san2"};
  ON_CALL(*connection_info, uriSanPeerCertificate()).WillByDefault(Return(sans));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", "san1,san2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerUriSanEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, uriSanPeerCertificate())
      .WillByDefault(Return(std::vector<std::string>()));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableSingleSan) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san"};
  ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(sans));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", "san");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableMultipleSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"san1", "san2"};
  ON_CALL(*connection_info, uriSanLocalCertificate()).WillByDefault(Return(sans));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", "san1,san2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanVariableNoSans) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, uriSanLocalCertificate())
      .WillByDefault(Return(std::vector<std::string>()));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalUriSanNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_URI_SAN", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubject) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject = "subject";
  ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(subject));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", "subject");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubjectEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject;
  ON_CALL(*connection_info, subjectLocalCertificate()).WillByDefault(ReturnRef(subject));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamLocalSubjectNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_LOCAL_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionId) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string session_id = "deadbeef";
  ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(session_id));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", "deadbeef");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionIdEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string session_id;
  ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(session_id));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsSessionIdNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_TLS_SESSION_ID", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipher) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, ciphersuiteString())
      .WillByDefault(Return("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipherEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, ciphersuiteString()).WillByDefault(Return(""));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsCipherNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_TLS_CIPHER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersion) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string tls_version = "TLSv1.2";
  ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(tls_version));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", "TLSv1.2");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersionEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, tlsVersion()).WillByDefault(ReturnRef(EMPTY_STRING));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamTlsVersionNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_TLS_VERSION", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha256Fingerprint) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
  ON_CALL(*connection_info, sha256PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256",
                 "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha256FingerprintEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha;
  ON_CALL(*connection_info, sha256PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha256FingerprintNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_256", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha1Fingerprint) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
  ON_CALL(*connection_info, sha1PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_1",
                 "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha1FingerprintEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_sha;
  ON_CALL(*connection_info, sha1PeerCertificateDigest()).WillByDefault(ReturnRef(expected_sha));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_1", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSha1FingerprintNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_FINGERPRINT_1", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerial) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string serial_number = "b8b5ecc898f2124a";
  ON_CALL(*connection_info, serialNumberPeerCertificate()).WillByDefault(ReturnRef(serial_number));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", "b8b5ecc898f2124a");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerialEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string serial_number;
  ON_CALL(*connection_info, serialNumberPeerCertificate()).WillByDefault(ReturnRef(serial_number));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSerialNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SERIAL", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuer) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string issuer_peer =
      "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
  ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(issuer_peer));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER",
                 "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuerEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string issuer_peer;
  ON_CALL(*connection_info, issuerPeerCertificate()).WillByDefault(ReturnRef(issuer_peer));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerIssuerNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_ISSUER", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubject) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string subject_peer =
      "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
  ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(subject_peer));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT",
                 "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubjectEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  const std::string subject_peer;
  ON_CALL(*connection_info, subjectPeerCertificate()).WillByDefault(ReturnRef(subject_peer));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerSubjectNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_SUBJECT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCert) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_cert = "<some cert>";
  ON_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillByDefault(ReturnRef(expected_cert));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", expected_cert);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string expected_cert;
  ON_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillByDefault(ReturnRef(expected_cert));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStart) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, validFromPeerCertificate()).WillByDefault(Return(startTime));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", "2018-12-18T01:50:34.000Z");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStartCustom) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, validFromPeerCertificate()).WillByDefault(Return(startTime));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START(%b %e %H:%M:%S %Y %Z)",
                 "Dec 18 01:50:34 2018 UTC");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStartEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, validFromPeerCertificate()).WillByDefault(Return(absl::nullopt));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVStartNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_START", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEnd) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, expirationPeerCertificate()).WillByDefault(Return(startTime));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", "2020-12-17T01:50:34.000Z");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEndCustom) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  ON_CALL(*connection_info, expirationPeerCertificate()).WillByDefault(Return(startTime));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END(%b %e %H:%M:%S %Y %Z)",
                 "Dec 17 01:50:34 2020 UTC");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEndEmpty) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, expirationPeerCertificate()).WillByDefault(Return(absl::nullopt));
  stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithDownstreamPeerCertVEndNoTls) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  testFormatting(stream_info, "DOWNSTREAM_PEER_CERT_V_END", EMPTY_STRING);
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithStartTime) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(startTime));
  testFormatting(stream_info, "START_TIME", "2020-12-17T01:50:34.000Z");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithStartTimeCustom) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::Time abslStartTime =
      TestUtility::parseTime("Dec 17 01:50:34 2020 GMT", "%b %e %H:%M:%S %Y GMT");
  SystemTime startTime = absl::ToChronoTime(abslStartTime);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(startTime));
  testFormatting(stream_info, "START_TIME(%b %e %H:%M:%S %Y %Z)", "Dec 17 01:50:34 2020 UTC");
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

  stream_info.upstreamInfo()->setUpstreamHost(host);
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
    header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
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
  stream_info.upstreamInfo()->setUpstreamHost(host);

  testFormatting(stream_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithInvalidUpstreamMetadata) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  stream_info.upstreamInfo()->setUpstreamHost(host);

  EXPECT_THROW_WITH_MESSAGE(
      testFormatting(stream_info, "UPSTREAM_METADATA(1)", ""), EnvoyException,
      "Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
      "...]), actual format UPSTREAM_METADATA1");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithRequestMetadata) {
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Struct struct_obj;

  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["foo"] = ValueUtil::stringValue("bar");
  (*metadata.mutable_filter_metadata())["envoy.lb"] = struct_obj;

  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  testFormatting(stream_info, "DYNAMIC_METADATA([\"envoy.lb\", \"foo\"])", "bar");
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithPerRequestStateVariable) {
  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ("test_value", filter_state->getDataReadOnly<StringAccessor>("testing")->asString());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  testFormatting(stream_info, "PER_REQUEST_STATE(testing)", "test_value");
  testFormatting(stream_info, "PER_REQUEST_STATE(testing2)", "");
  EXPECT_EQ("test_value", filter_state->getDataReadOnly<StringAccessor>("testing")->asString());
}

TEST_F(StreamInfoHeaderFormatterTest, TestFormatWithNonStringPerRequestStateVariable) {
  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StreamInfo::TestIntAccessor>(1),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(1, filter_state->getDataReadOnly<StreamInfo::TestIntAccessor>("testing")->access());

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  testFormatting(stream_info, "PER_REQUEST_STATE(testing)", "");
}

TEST_F(StreamInfoHeaderFormatterTest, WrongFormatOnPerRequestStateVariable) {
  // No parameters
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE()"), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE()");

  // Missing single parens
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE(testing"), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE(testing");
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("PER_REQUEST_STATE testing)"), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE testing)");
}

TEST_F(StreamInfoHeaderFormatterTest, UnknownVariable) { testInvalidFormat("INVALID_VARIABLE"); }

TEST_F(StreamInfoHeaderFormatterTest, WrongFormatOnUpstreamMetadataVariable) {
  // Invalid JSON.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA(abcd)"), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(abcd), because JSON supplied is not valid. "
                            "Error(line 1, column 1, token a): syntax error while parsing value - "
                            "invalid literal; last read: 'a'\n");

  // No parameters.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA"), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA");

  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA()"), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format UPSTREAM_METADATA(), "
      "because JSON supplied is not valid. Error(line 1, column 1, token ): syntax error while "
      "parsing value - unexpected end of input; expected '[', '{', or a literal\n");

  // One parameter.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"ns\"])"),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"ns\"])");

  // Missing close paren.
  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA("), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(");

  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([a,b,c,d]"),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([a,b,c,d]");

  EXPECT_THROW_WITH_MESSAGE(StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\",\"b\"]"),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"a\",\"b\"]");

  // Non-string elements.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", 1])"), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", 1]), because JSON field from line 1 accessed with type 'String' "
      "does not match actual type 'Integer'.");

  // Invalid string elements.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", \"\\unothex\"])"), EnvoyException,
      "Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", "
      "\"k\", ...]), actual format UPSTREAM_METADATA([\"a\", \"\\unothex\"]), because JSON "
      "supplied is not valid. Error(line 1, column 10, token \"\\un): syntax error while parsing "
      "value - invalid string: '\\u' must be followed by 4 hex digits; last read: '\"\\un'\n");

  // Non-array parameters.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter("UPSTREAM_METADATA({\"a\":1})"), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA({\"a\":1}), because JSON field from line 1 accessed with type 'Array' "
      "does not match actual type 'Object'.");

  // Number overflow.
  EXPECT_THROW_WITH_MESSAGE(
      StreamInfoHeaderFormatter(
          "UPSTREAM_METADATA(-"
          "5211111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
          "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
          "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
          "1111111111111111111111111111111111111111111111111)"),
      EnvoyException,
      "Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
      "...]), actual format "
      "UPSTREAM_METADATA(-"
      "52111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "1111111111111111111111111111111111111), because JSON supplied is not valid. Error(position: "
      "314):  number overflow parsing '-"
      "52111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
      "1111111111111111111111111111111111111'\n");
}

TEST(PlainFormatterTest, BasicTest) { PlainHeaderFormatter formatter("test"); }

TEST(CompoundFormatterTest, BasicTest) {
  std::vector<HeaderFormatterPtr> formatters;
  formatters.push_back(std::make_unique<PlainHeaderFormatter>("test1"));
  formatters.push_back(std::make_unique<PlainHeaderFormatter>("test2"));

  CompoundHeaderFormatter formatter(std::move(formatters));
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
      {"%DOWNSTREAM_REMOTE_ADDRESS%", {"127.0.0.1:0"}, {}},
      {"%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.1"}, {}},
      {"%DOWNSTREAM_REMOTE_PORT%", {"0"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%", {"127.0.0.3:63443"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.3"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_PORT%", {"63443"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS%", {"127.0.0.2:0"}, {}},
      {"%DOWNSTREAM_LOCAL_PORT%", {"0"}, {}},
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
      {"%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"10.0.0.1"}, {}},
      {"%UPSTREAM_REMOTE_PORT%", {"443"}, {}},
      {"%UPSTREAM_LOCAL_ADDRESS%", {"127.0.0.3:8443"}, {}},
      {"%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%", {"127.0.0.3"}, {}},
      {"%UPSTREAM_LOCAL_PORT%", {"8443"}, {}},
      {"%REQUESTED_SERVER_NAME%", {"foo.bar"}, {}},
      {"%VIRTUAL_CLUSTER_NAME%", {"authN"}, {}},
      {"%PER_REQUEST_STATE(testing)%", {"test_value"}, {}},
      {"%REQ(x-request-id)%", {"123"}, {}},
      {"%START_TIME%", {"2018-04-03T23:06:09.123Z"}, {}},
      {"%RESPONSE_FLAGS%", {"LR"}, {}},
      {"%RESPONSE_CODE_DETAILS%", {"via_upstream"}, {}},
      {"STATIC_TEXT", {"STATIC_TEXT"}, {}},

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
      {"%UPSTREAM_METADATA([\"\\",
       {},
       {"Invalid header configuration. Un-terminated backslash in JSON string after "
        "'UPSTREAM_METADATA([\"'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"]x",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"]', but found 'x'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])% %UPSTREAM_METADATA([\"ns\", \"key\"]x",
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

      {"%REQ%",
       {},
       {"Invalid header configuration. Expected format REQ(<header-name>), "
        "actual format REQ"}},
      {"%REQ no parens%",
       {},
       {"Invalid header configuration. Expected format REQ(<header-name>), "
        "actual format REQno parens"}},

      // Invalid arguments
      {"%UPSTREAM_METADATA%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA"}},
  };

  /*
    The following test cases do not make sense after using unified header formatters.
    See issue 20389. The test cases are executed only when runtime guard
    envoy_reloadable_features_unified_header_formatter is false.
    Comments below explain why unified header formatter parser will not fail.
    TODO(cpakulski): the following test cases should be removed when
    envoy_reloadable_features_unified_header_formatter is deprecated.
    */
  static const TestCase obsolete_test_cases[] = {
      // Single key is allowed in UPSTREAM_METADATA
      {"%UPSTREAM_METADATA(no array)%",
       {},
       {"Invalid header configuration. Expected format "
        "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
        "UPSTREAM_METADATA(no array), because JSON supplied is not valid. Error(line 1, "
        "column 2, token no): syntax error while parsing value - invalid literal; last read: "
        "'no'\n"}},
      // Single key is allowed in UPSTREAM_METADATA
      {"%UPSTREAM_METADATA( no array)%",
       {},
       {"Invalid header configuration. Expected format "
        "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format UPSTREAM_METADATA( "
        "no array), because JSON supplied is not valid. Error(line 1, column 3, token  no): "
        "syntax error while parsing value - invalid literal; last read: ' no'\n"}},
      // [\"unterminated array\" will treated as key.
      {"%UPSTREAM_METADATA([\"unterminated array\")%",
       {},
       {"Invalid header configuration. Expecting ',', ']', or whitespace after "
        "'UPSTREAM_METADATA([\"unterminated array\"', but found ')'"}},
      // [not-a-string] will be treated as key.
      {"%UPSTREAM_METADATA([not-a-string])%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'UPSTREAM_METADATA([', "
        "but found 'n'"}},
      // [\"ns\"] will be treated as string.
      {"%UPSTREAM_METADATA([\"ns\"])%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA([\"ns\"])"}},
      {"%START_TIME(%85n)%", {}, {"Invalid header configuration. Format string contains newline."}},

  };

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const std::string requested_server_name = "foo.bar";
  stream_info.downstream_connection_info_provider_->setRequestedServerName(requested_server_name);
  const std::string virtual_cluster_name = "authN";
  stream_info.setVirtualClusterName(virtual_cluster_name);
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  stream_info.upstreamInfo()->setUpstreamHost(host);
  auto local_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.3", 8443)};
  stream_info.upstreamInfo()->setUpstreamLocalAddress(local_address);

  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::LowerCaseString(std::string("x-request-id")), 123);
  ON_CALL(stream_info, getRequestHeaders()).WillByDefault(Return(&request_headers));

  // Upstream metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
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

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::LocalReset))
      .WillByDefault(Return(true));

  absl::optional<std::string> rc_details{"via_upstream"};
  ON_CALL(stream_info, responseCodeDetails()).WillByDefault(ReturnRef(rc_details));

  // Run all tests twice: once using old parser and again using access log's parser.
  for (const bool use_unified_parser : std::vector<bool>{false, true}) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.unified_header_formatter",
                                  use_unified_parser);
    for (const auto& test_case : test_cases) {
      Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> to_add;
      envoy::config::core::v3::HeaderValueOption* header = to_add.Add();
      header->mutable_header()->set_key("x-header");
      header->mutable_header()->set_value(test_case.input_);

      if (test_case.expected_exception_) {
        EXPECT_FALSE(test_case.expected_output_);
        if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_header_formatter")) {
          EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(to_add), EnvoyException,
                                    test_case.expected_exception_.value());
        } else {
          EXPECT_THROW(HeaderParser::configure(to_add), EnvoyException);
        }
        continue;
      }

      HeaderParserPtr req_header_parser = HeaderParser::configure(to_add);

      Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
      req_header_parser->evaluateHeaders(header_map, stream_info);

      std::string descriptor = fmt::format("for test case input: {}", test_case.input_);

      if (!test_case.expected_output_) {
        EXPECT_FALSE(header_map.has("x-header")) << descriptor;
        continue;
      }

      EXPECT_TRUE(header_map.has("x-header")) << descriptor;
      EXPECT_EQ(test_case.expected_output_.value(), header_map.get_("x-header")) << descriptor;
    }

    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_header_formatter")) {
      for (const auto& test_case : obsolete_test_cases) {
        Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> to_add;
        envoy::config::core::v3::HeaderValueOption* header = to_add.Add();
        header->mutable_header()->set_key("x-header");
        header->mutable_header()->set_value(test_case.input_);

        if (test_case.expected_exception_) {
          EXPECT_FALSE(test_case.expected_output_);
          EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(to_add), EnvoyException,
                                    test_case.expected_exception_.value());
          continue;
        }
      }
    }
  }
}

TEST(HeaderParser, TestMetadataTranslator) {
  struct TestCase {
    std::string input_;
    std::string expected_output_;
  };
  static const TestCase test_cases[] = {
      {"%UPSTREAM_METADATA([\"a\", \"b\"])%", "%UPSTREAM_METADATA(a:b)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])%", "%UPSTREAM_METADATA(a:b:c)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% %UPSTREAM_METADATA([\"d\", \"e\"])%",
       "%UPSTREAM_METADATA(a:b:c)% %UPSTREAM_METADATA(d:e)%"},
      {"%DYNAMIC_METADATA([\"a\", \"b\",\"c\"])%", "%DYNAMIC_METADATA(a:b:c)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% LEAVE_IT %DYNAMIC_METADATA([\"d\", \"e\"])%",
       "%UPSTREAM_METADATA(a:b:c)% LEAVE_IT %DYNAMIC_METADATA(d:e)%"},
      // The following test cases contain parts which should not be translated.
      {"nothing to translate", "nothing to translate"},
      {"%UPSTREAM_METADATA([\"a\", \"b\")%", "%UPSTREAM_METADATA([\"a\", \"b\")%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\"]])%", "%UPSTREAM_METADATA([\"a\", \"b\"]])%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% %DYNAMIC_METADATA([\"d\", \"e\")%",
       "%UPSTREAM_METADATA(a:b:c)% %DYNAMIC_METADATA([\"d\", \"e\")%"},
      {"UPSTREAM_METADATA([\"a\", \"b\"])%", "UPSTREAM_METADATA([\"a\", \"b\"])%"}};

  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case.expected_output_, HeaderParser::translateMetadataFormat(test_case.input_));
  }
}

// Test passing incorrect json. translateMetadataFormat should return
// the same value without any modifications.
TEST(HeaderParser, TestMetadataTranslatorExceptions) {
  static const std::string test_cases[] = {
      "%UPSTREAM_METADATA([\"a\" - \"b\"])%",
      "%UPSTREAM_METADATA(\t [ \t\t ] \t)%",
      "%UPSTREAM_METADATA([\"udp{VTA(r%%%%%TA(r%%%%%b\\\\\\rin\\rsE(r%%%%%b\\\\\\rsi",
  };
  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case, HeaderParser::translateMetadataFormat(test_case));
  }
}

TEST(HeaderParser, TestPerFilterStateTranslator) {
  struct TestCase {
    std::string input_;
    std::string expected_output_;
  };
  static const TestCase test_cases[] = {
      {"%PER_REQUEST_STATE(some-state)%", "%FILTER_STATE(some-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(some-state:other-state)%",
       "%FILTER_STATE(some-state:other-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(some-state)% %PER_REQUEST_STATE(other-state)%",
       "%FILTER_STATE(some-state:PLAIN)% %FILTER_STATE(other-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(\\0)%", "%FILTER_STATE(\\0:PLAIN)%"},
      {"%PER_REQUEST_STATE(\\1)%", "%FILTER_STATE(\\1:PLAIN)%"},
  };

  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case.expected_output_, HeaderParser::translatePerRequestState(test_case.input_));
  }
}

TEST(HeaderParserTest, EvaluateHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
  - header:
      key: "x-client-port"
      value: "%DOWNSTREAM_REMOTE_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
  EXPECT_TRUE(header_map.has("x-client-port"));
}

TEST(HeaderParserTest, EvaluateHeadersAppendIfEmpty) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-upstream-remote-address"
      value: "%UPSTREAM_REMOTE_ADDRESS%"
    append_action: APPEND_IF_EXISTS_OR_ADD
    keep_empty_value: true
  - header:
      key: "x-upstream-local-port"
      value: "%UPSTREAM_LOCAL_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  stream_info.upstreamInfo()->setUpstreamLocalAddress(nullptr);
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-upstream-local-port"));
  EXPECT_TRUE(header_map.has("x-upstream-remote-address"));
}

TEST(HeaderParserTest, EvaluateHeadersWithNullStreamInfo) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
  - header:
      key: "x-client-port"
      value: "%DOWNSTREAM_REMOTE_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  req_header_parser->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
  EXPECT_TRUE(header_map.has("x-client-port"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", header_map.get_("x-client-ip"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS%", header_map.get_("x-client-ip-port"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_PORT%", header_map.get_("x-client-port"));
}

TEST(HeaderParserTest, EvaluateHeaderValuesWithNullStreamInfo) {
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue> headers_values;

  auto& first_entry = *headers_values.Add();
  first_entry.set_key("key");

  // This tests when we have "StreamInfoHeaderFormatter", but stream info is null.
  first_entry.set_value("%DOWNSTREAM_REMOTE_ADDRESS%");

  HeaderParserPtr req_header_parser_add =
      HeaderParser::configure(headers_values, HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  req_header_parser_add->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("key"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS%", header_map.get_("key"));

  headers_values.Clear();
  auto& set_entry = *headers_values.Add();
  set_entry.set_key("key");
  set_entry.set_value("great");

  HeaderParserPtr req_header_parser_set =
      HeaderParser::configure(headers_values, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  req_header_parser_set->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("key"));
  EXPECT_EQ("great", header_map.get_("key"));

  headers_values.Clear();
  auto& empty_entry = *headers_values.Add();
  empty_entry.set_key("empty");
  empty_entry.set_value("");

  HeaderParserPtr req_header_parser_empty =
      HeaderParser::configure(headers_values, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  req_header_parser_empty->evaluateHeaders(header_map, nullptr);
  EXPECT_FALSE(header_map.has("empty"));
}

TEST(HeaderParserTest, EvaluateEmptyHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-key"
      value: "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  stream_info.upstreamInfo()->setUpstreamHost(host);
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-key"));
}

TEST(HeaderParserTest, EvaluateStaticHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
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

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  stream_info.upstreamInfo()->setUpstreamHost(host);

  // Metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            "%key%": value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

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
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s%3f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  // Disable append mode.
  envoy::config::route::v3::Route route = parseRouteFromV3Yaml(yaml);
  route.mutable_request_headers_to_add(0)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  route.mutable_request_headers_to_add(1)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  route.mutable_request_headers_to_add(2)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route.request_headers_to_add());
  Http::TestRequestHeaderMapImpl header_map{
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
  header_map.iterate([&counts](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    absl::string_view key = header.key().getStringView();
    CountMap::iterator i = counts.find(key);
    if (i == counts.end()) {
      counts.insert({std::string(key), 1});
    } else {
      i->second++;
    }
    return Http::HeaderMap::Iterate::Continue;
  });

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
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s.%3f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-multiple"
      value: "%START_TIME(%s.%3f)% %START_TIME% %START_TIME(%s)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-f"
      value: "%START_TIME(f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "set-cookie"
      value: "foo"
  - header:
      key: "set-cookie"
      value: "bar"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-upstream-req-id"
      value: "%RESP(x-resp-id)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-downstream-req-id"
      value: "%REQ(x-req-id)%"
    append_action: APPEND_IF_EXISTS_OR_ADD

response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestRequestHeaderMapImpl request_header_map{{":method", "POST"}, {"x-req-id", "543"}};
  Http::TestResponseHeaderMapImpl response_header_map{
      {"x-safe", "safe"}, {"x-nope", "nope"}, {"x-resp-id", "321"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Initialize start_time as 2018-04-03T23:06:09.123Z in microseconds.
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(7).WillRepeatedly(Return(start_time));

  resp_header_parser->evaluateHeaders(response_header_map, request_header_map, response_header_map,
                                      stream_info);
  EXPECT_TRUE(response_header_map.has("x-client-ip"));
  EXPECT_TRUE(response_header_map.has("x-client-ip-port"));
  EXPECT_TRUE(response_header_map.has("x-request-start-multiple"));
  EXPECT_TRUE(response_header_map.has("x-safe"));
  EXPECT_FALSE(response_header_map.has("x-nope"));
  EXPECT_TRUE(response_header_map.has("x-request-start"));
  EXPECT_EQ("1522796769.123", response_header_map.get_("x-request-start"));
  EXPECT_EQ("1522796769.123 2018-04-03T23:06:09.123Z 1522796769",
            response_header_map.get_("x-request-start-multiple"));
  EXPECT_TRUE(response_header_map.has("x-request-start-f"));
  EXPECT_EQ("f", response_header_map.get_("x-request-start-f"));
  EXPECT_TRUE(response_header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", response_header_map.get_("x-request-start-default"));
  EXPECT_TRUE(response_header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            response_header_map.get_("x-request-start-range"));
  EXPECT_EQ("foo", response_header_map.get_("set-cookie"));
  EXPECT_EQ("321", response_header_map.get_("x-upstream-req-id"));
  EXPECT_EQ("543", response_header_map.get_("x-downstream-req-id"));

  // Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
  // combine set-cookie headers
  const auto out = response_header_map.get(Http::LowerCaseString("set-cookie"));
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0]->value().getStringView(), "foo");
  ASSERT_EQ(out[1]->value().getStringView(), "bar");
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

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestRequestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

TEST(HeaderParserTest, EvaluateRequestHeadersAddIfAbsent) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-header"
      value: "per"
    append_action: ADD_IF_ABSENT
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser = HeaderParser::configure(route.response_headers_to_add());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestResponseHeaderMapImpl header_map;
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "exist-foo"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ(2, header_map.get(Http::LowerCaseString("x-foo-header")).size());
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-bar-header", "exist-bar"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ(1, header_map.get(Http::LowerCaseString("x-bar-header")).size());
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-per-header", "exist-per"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("exist-per", header_map.get_("x-per-header"));
  }
}

TEST(HeaderParserTest, DEPRECATED_FEATURE_TEST(EvaluateRequestHeadersAddWithDeprecatedAppend)) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append: true
  - header:
      key: "x-bar-header"
      value: "bar"
    append: false
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser = HeaderParser::configure(route.response_headers_to_add());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestResponseHeaderMapImpl header_map;
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "exist-foo"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ(2, header_map.get(Http::LowerCaseString("x-foo-header")).size());
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-bar-header", "exist-bar"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ(1, header_map.get(Http::LowerCaseString("x-bar-header")).size());
  }
}

TEST(HeaderParserTest,
     DEPRECATED_FEATURE_TEST(EvaluateRequestHeadersAddWithDeprecatedAppendAndAction)) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append: true
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append: false
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);

  EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(route.response_headers_to_add()),
                            EnvoyException,
                            "Both append and append_action are set and it's not allowed");
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

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

TEST(HeaderParserTest, GetHeaderTransformsWithFormatting) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-request-header"
      value: "%PER_REQUEST_STATE(testing)%"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  auto transforms = resp_header_parser->getHeaderTransforms(stream_info);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar"),
                          Pair(Http::LowerCaseString("x-per-request-header"), "test_value")));
  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

TEST(HeaderParserTest, GetHeaderTransformsOriginalValues) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-request-header"
      value: "%PER_REQUEST_STATE(testing)%"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr response_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  auto transforms =
      response_header_parser->getHeaderTransforms(stream_info, /*do_formatting=*/false);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar"),
                          Pair(Http::LowerCaseString("x-per-request-header"),
                               "%PER_REQUEST_STATE(testing)%")));
  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

TEST(HeaderParserTest, GetHeaderTransformsForAllActions) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-header"
      value: "per"
    append_action: ADD_IF_ABSENT
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr response_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  auto transforms =
      response_header_parser->getHeaderTransforms(stream_info, /*do_formatting=*/false);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar")));
  EXPECT_THAT(transforms.headers_to_add_if_absent,
              ElementsAre(Pair(Http::LowerCaseString("x-per-header"), "per")));

  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

} // namespace
} // namespace Router
} // namespace Envoy
