#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using SyslogAccessLogConfig = envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig;
using testing::Eq;
using testing::HasSubstr;

class SyslogAccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  SyslogAccessLogIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()),
        bind_address_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(GetParam()), 0)),
        receiver_(Network::Socket::Type::Datagram, bind_address_, nullptr,
                  Network::SocketCreationOptions{}) {
    skip_tag_extraction_rule_check_ = true;
    autonomous_upstream_ = true;
    EXPECT_EQ(0, receiver_.bind(bind_address_).return_value_);
  }

  uint32_t syslogPort() const {
    return receiver_.connectionInfoProvider().localAddress()->ip()->port();
  }

  std::string loopbackAddress() const {
    return Network::Test::getLoopbackAddressString(GetParam());
  }

  void initializeWithSyslog(const SyslogAccessLogConfig& access_log_config,
                            bool add_syslog_cluster = false) {
    if (add_syslog_cluster) {
      config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
        cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        cluster->set_name("syslog");
        cluster->clear_load_assignment();
        cluster->mutable_load_assignment()->set_cluster_name("syslog");
        auto* socket_address = cluster->mutable_load_assignment()
                                   ->add_endpoints()
                                   ->add_lb_endpoints()
                                   ->mutable_endpoint()
                                   ->mutable_address()
                                   ->mutable_socket_address();
        socket_address->set_address(loopbackAddress());
        socket_address->set_port_value(syslogPort());
      });
    }

    config_helper_.addConfigModifier(
        [access_log_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.access_loggers.syslog");
          std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
        });

    HttpIntegrationTest::initialize();
  }

  SyslogAccessLogConfig makeBaseConfig() {
    SyslogAccessLogConfig config;
    config.set_no_hostname(true);
    config.set_stat_prefix("test");
    config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%REQ(:METHOD)% %REQ(:PATH)% %RESPONSE_CODE%");
    return config;
  }

  SyslogAccessLogConfig makeClusterConfig() {
    auto config = makeBaseConfig();
    config.set_cluster("syslog");
    return config;
  }

  void sendRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/syslog"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  static std::string receiveDatagram(Network::SocketImpl& socket) {
    std::string buffer(2048, '\0');
    const Api::IoCallUint64Result result = socket.ioHandle().recv(buffer.data(), buffer.size(), 0);
    EXPECT_TRUE(result.ok());
    if (!result.ok()) {
      return {};
    }
    buffer.resize(result.return_value_);
    return buffer;
  }

  std::string expectLoggedPayload(Network::SocketImpl& socket) {
    test_server_->waitForCounter("access_logs.syslog.test.send", Eq(1));
    std::string datagram = receiveDatagram(socket);
    EXPECT_THAT(datagram, HasSubstr("GET /syslog 200"));
    return datagram;
  }

#ifndef WIN32
  static Network::Address::InstanceConstSharedPtr unixDestination() {
#ifdef __linux__
    return Network::Address::PipeInstance::create(absl::StrCat("@", TestUtility::uniqueFilename()))
        .value();
#else
    return Network::Address::PipeInstance::create(
               TestEnvironment::temporaryPath(TestUtility::uniqueFilename()))
        .value();
#endif
  }

  SyslogAccessLogConfig makePipeConfig(absl::string_view path) {
    auto config = makeBaseConfig();
    config.mutable_pipe()->set_path(std::string(path));
    return config;
  }
#endif

  Network::Address::InstanceConstSharedPtr bind_address_;
  Network::SocketImpl receiver_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SyslogAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SyslogAccessLogIntegrationTest, ClusterDestination) {
  initializeWithSyslog(makeClusterConfig(), /*add_syslog_cluster=*/true);
  sendRequest();

  test_server_->waitForCounter("access_logs.syslog.test.messages.state.full", Eq(1));
  const std::string datagram = expectLoggedPayload(receiver_);
  EXPECT_EQ(datagram.size(), test_server_->counter("access_logs.syslog.test.bytes_sent")->value());
}

#ifndef WIN32
TEST_P(SyslogAccessLogIntegrationTest, PipeDestination) {
  if (GetParam() != Network::Address::IpVersion::v4) {
    GTEST_SKIP();
  }

  Network::Address::InstanceConstSharedPtr destination = unixDestination();
  Network::SocketImpl unix_receiver(Network::Socket::Type::Datagram, destination, nullptr,
                                    Network::SocketCreationOptions{});
  ASSERT_EQ(0, unix_receiver.bind(destination).return_value_);

  initializeWithSyslog(makePipeConfig(destination->asString()));
  sendRequest();
  expectLoggedPayload(unix_receiver);
}

TEST_P(SyslogAccessLogIntegrationTest, LoggingFailureDoesNotFailRequest) {
  if (GetParam() != Network::Address::IpVersion::v4) {
    GTEST_SKIP();
  }

  // Nothing is bound to this socket, so the datagram send fails.
  const auto destination = unixDestination();
  initializeWithSyslog(makePipeConfig(destination->asString()));
  sendRequest();

  test_server_->waitForCounter("access_logs.syslog.test.messages.state.full", Eq(1));
  EXPECT_EQ(0, test_server_->counter("access_logs.syslog.test.send")->value());
  EXPECT_EQ(0, test_server_->counter("access_logs.syslog.test.bytes_sent")->value());
}
#endif

} // namespace
} // namespace Envoy
