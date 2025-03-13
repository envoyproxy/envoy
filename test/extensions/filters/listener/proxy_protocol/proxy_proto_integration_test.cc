#include "test/extensions/filters/listener/proxy_protocol/proxy_proto_integration_test.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {

using ::testing::IsSupersetOf;

constexpr absl::string_view kProxyProtoFilterName = "envoy.listener.proxy_protocol";

static void
insertProxyProtocolFilterConfigModifier(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  ::envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;
  auto rule = proxy_protocol.add_rules();
  rule->set_tlv_type(0x02);
  rule->mutable_on_tlv_present()->set_key("PP2TypeAuthority");

  auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
  listener->set_stat_prefix("test_listener");
  auto* ppv_filter = listener->add_listener_filters();
  ppv_filter->set_name(kProxyProtoFilterName);
  ASSERT_TRUE(ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol));
}

ProxyProtoIntegrationTest::ProxyProtoIntegrationTest()
    : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
  config_helper_.addConfigModifier(insertProxyProtocolFilterConfigModifier);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ProxyProtoIntegrationTest, CaptureTlvToMetadata) {
  useListenerAccessLog(
      "%DYNAMIC_METADATA(envoy.filters.listener.proxy_protocol:PP2TypeAuthority)%");

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a, 0x21, 0x11, 0x00, 0x1a, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01,
                                  0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 0x00, 0x00, 0x01, 0xff, 0x02,
                                  0x00, 0x07, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  cleanupUpstreamAndDownstream();
  const std::string log_line = waitForAccessLog(listener_access_log_name_);
  EXPECT_EQ(log_line, "foo.com");
}

TEST_P(ProxyProtoIntegrationTest, V1RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V2RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                  0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                  0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0x04, 0xd2};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V1RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP6 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V2RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a, 0x21, 0x22, 0x00, 0x24, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                  0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02};
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);

  // Verify stats (with tags for proxy protocol version, but no stat prefix).
  const auto found_counter = test_server_->counter("proxy_proto.versions.v2.found");
  ASSERT_NE(found_counter.get(), nullptr);
  EXPECT_EQ(found_counter->value(), 1UL);
  EXPECT_EQ(found_counter->tagExtractedName(), "proxy_proto.found");
  EXPECT_THAT(found_counter->tags(), IsSupersetOf(Stats::TagVector{
                                         {"envoy.proxy_protocol_version", "2"},
                                     }));
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownLongRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that %DOWNSTREAM_DIRECT_REMOTE_ADDRESS%/%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%
// returns the direct address, and %DOWSTREAM_REMOTE_ADDRESS% returns the proxy-protocol-provided
// address.
TEST_P(ProxyProtoIntegrationTest, AccessLog) {
  useAccessLog("%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT% %DOWNSTREAM_REMOTE_ADDRESS%");

  // Tell HCM to ignore x-forwarded-for so that the proxy-proto address is used.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_use_remote_address()->set_value(true); });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  const std::string log_line = waitForAccessLog(access_log_name_);
  const std::vector<absl::string_view> tokens = StringUtil::splitToken(log_line, " ", false, true);

  ASSERT_EQ(2, tokens.size());
  EXPECT_EQ(tokens[0], Network::Test::getLoopbackAddressString(GetParam()));
  EXPECT_EQ(tokens[1], "1.2.3.4:12345");
}

TEST_P(ProxyProtoIntegrationTest, ClusterProvided) {
  // Change the cluster to an original destination cluster. An original destination cluster
  // ignores the configured hosts, and instead uses the restored destination address from the
  // incoming (server) connection as the destination address for the outgoing (client) connection.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->clear_load_assignment();
    cluster->set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
    cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Create proxy protocol line that has the fake upstream address as the destination address.
    // This address will become the "restored" address for the server connection and will
    // be used as the destination address by the original destination cluster.
    std::string proxyLine = fmt::format(
        "PROXY {} {} 65535 {}\r\n",
        GetParam() == Network::Address::IpVersion::v4 ? "TCP4 1.2.3.4" : "TCP6 1:2:3::4",
        Network::Test::getLoopbackAddressString(GetParam()),
        fake_upstreams_[0]->localAddress()->ip()->port());

    Buffer::OwnedImpl buf(proxyLine);
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

ProxyProtoTcpIntegrationTest::ProxyProtoTcpIntegrationTest()
    : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
  config_helper_.addConfigModifier(insertProxyProtocolFilterConfigModifier);
  config_helper_.renameListener("tcp_proxy");
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This tests that the StreamInfo contains the correct addresses.
TEST_P(ProxyProtoTcpIntegrationTest, AccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "remote=%DOWNSTREAM_REMOTE_ADDRESS% local=%DOWNSTREAM_LOCAL_ADDRESS%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\nhello", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  std::string log_result;
  // Access logs only get flushed to disk periodically, so poll until the log is non-empty
  do {
    log_result = api_->fileSystem().fileReadToEnd(access_log_path).value();
  } while (log_result.empty());

  EXPECT_EQ(log_result, "remote=1.2.3.4:12345 local=254.254.254.254:1234");
}

ProxyProtoFilterChainMatchIntegrationTest::ProxyProtoFilterChainMatchIntegrationTest() {
  useListenerAccessLog("%FILTER_CHAIN_NAME% %RESPONSE_CODE_DETAILS%");
  config_helper_.skipPortUsageValidation();
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // This test doesn't need to deal with upstream connections at all, so make sure none occur.
    bootstrap.mutable_static_resources()->mutable_clusters(0)->clear_load_assignment();

    auto* orig_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    for (unsigned i = 0; i < 3; i++) {
      *bootstrap.mutable_static_resources()->mutable_listeners(0)->add_filter_chains() =
          *orig_filter_chain;
    }

    auto setPrefix = [](auto* prefix, const std::string& ip, uint32_t length) {
      prefix->set_address_prefix(ip);
      prefix->mutable_prefix_len()->set_value(length);
    };

    {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      filter_chain->set_name("directsource_localhost_and_source_1.2.3.0/24");

      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "127.0.0.1", 8);
      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "::1", 128);

      setPrefix(filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges(), "1.2.3.0",
                24);
    }

    {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(1);
      filter_chain->set_name("wrong_directsource_and_source_1.2.3.0/24");
      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "1.1.1.1", 32);
      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "eeee::1", 128);

      setPrefix(filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges(), "1.2.3.0",
                24);
    }

    {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(2);
      filter_chain->set_name("wrong_directsource_and_source_5.5.5.5.32");
      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "1.1.1.1", 32);
      setPrefix(filter_chain->mutable_filter_chain_match()->add_direct_source_prefix_ranges(),
                "eeee::1", 128);

      setPrefix(filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges(), "5.5.5.5",
                32);
    }

    {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(3);
      filter_chain->set_name("no_direct_source_and_source_6.6.6.6/32");

      setPrefix(filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges(), "6.6.6.6",
                32);
    }
  });
}

void ProxyProtoFilterChainMatchIntegrationTest::send(const std::string& data) {
  initialize();

  // Set verify to false because it is expected that Envoy will immediately disconnect after
  // receiving the PROXY header, and it is a race whether the `write()` will fail due to
  // disconnect, or finish the write before receiving the disconnect.
  constexpr bool verify = false;

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write(data, false, verify));
  tcp_client->waitForDisconnect();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoFilterChainMatchIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that source IP and direct source IP match correctly.
TEST_P(ProxyProtoFilterChainMatchIntegrationTest, MatchDirectSourceAndSource) {
  send("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\nhello");
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr("directsource_localhost_and_source_1.2.3.0/24 -"));
}

// Test that a mismatched direct source prevents matching a filter chain with a matching source.
TEST_P(ProxyProtoFilterChainMatchIntegrationTest, MismatchDirectSourceButMatchSource) {
  send("PROXY TCP4 5.5.5.5 254.254.254.254 12345 1234\r\nhello");
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr(
                  absl::StrCat("- ", StreamInfo::ResponseCodeDetails::get().FilterChainNotFound)));
}

// Test that a more specific direct source match prevents matching a filter chain with a less
// specific direct source match but matching source.
TEST_P(ProxyProtoFilterChainMatchIntegrationTest, MoreSpecificDirectSource) {
  send("PROXY TCP4 6.6.6.6 254.254.254.254 12345 1234\r\nhello");
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr(
                  absl::StrCat("- ", StreamInfo::ResponseCodeDetails::get().FilterChainNotFound)));
}

ProxyProtoDisallowedVersionsIntegrationTest::ProxyProtoDisallowedVersionsIntegrationTest() {
  config_helper_.skipPortUsageValidation();
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // This test doesn't need to deal with upstream connections at all, so make sure none occur.
    bootstrap.mutable_static_resources()->mutable_clusters(0)->clear_load_assignment();

    // V1 is disallowed.
    ::envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;
    proxy_protocol.add_disallowed_versions(::envoy::config::core::v3::ProxyProtocolConfig::V1);
    proxy_protocol.set_stat_prefix("test_stat_prefix");

    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* ppv_filter = listener->mutable_listener_filters(0);
    ASSERT_EQ(ppv_filter->name(), kProxyProtoFilterName);
    // Overwrite.
    ASSERT_TRUE(ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol));
  });
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoDisallowedVersionsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate Envoy closes connection when PROXY protocol version 1 is used.
TEST_P(ProxyProtoDisallowedVersionsIntegrationTest, V1Disallowed) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\nhello",
                                /*end_stream=*/false, /*verify=*/false));
  tcp_client->waitForDisconnect();

  // Verify stats (with tags for proxy protocol version and stat prefix).
  const auto found_counter =
      test_server_->counter("proxy_proto.test_stat_prefix.versions.v1.found");
  ASSERT_NE(found_counter.get(), nullptr);
  EXPECT_EQ(found_counter->value(), 1UL);
  EXPECT_EQ(found_counter->tagExtractedName(), "proxy_proto.found");
  EXPECT_THAT(found_counter->tags(), IsSupersetOf(Stats::TagVector{
                                         {"envoy.proxy_protocol_version", "1"},
                                         {"envoy.proxy_protocol_prefix", "test_stat_prefix"},
                                     }));

  const auto disallowed_counter =
      test_server_->counter("proxy_proto.test_stat_prefix.versions.v1.disallowed");
  ASSERT_NE(disallowed_counter.get(), nullptr);
  EXPECT_EQ(disallowed_counter->value(), 1UL);
  EXPECT_EQ(disallowed_counter->tagExtractedName(), "proxy_proto.disallowed");
  EXPECT_THAT(disallowed_counter->tags(), IsSupersetOf(Stats::TagVector{
                                              {"envoy.proxy_protocol_version", "1"},
                                              {"envoy.proxy_protocol_prefix", "test_stat_prefix"},
                                          }));
}

// Validate Envoy closes connection when PROXY protocol version 2 has parsing error.
TEST_P(ProxyProtoDisallowedVersionsIntegrationTest, V2Error) {
  // A well-formed message with an unsupported address family
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x41, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};

  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  Buffer::OwnedImpl buf(buffer, sizeof(buffer));
  ASSERT_TRUE(tcp_client->write(buf.toString(), /*end_stream=*/false, /*verify=*/false));
  tcp_client->waitForDisconnect();

  // Verify stats (with tags for proxy protocol version and stat prefix).
  const auto found_counter =
      test_server_->counter("proxy_proto.test_stat_prefix.versions.v2.error");
  ASSERT_NE(found_counter.get(), nullptr);
  EXPECT_EQ(found_counter->value(), 1UL);
  EXPECT_EQ(found_counter->tagExtractedName(), "proxy_proto.error");
  EXPECT_THAT(found_counter->tags(), IsSupersetOf(Stats::TagVector{
                                         {"envoy.proxy_protocol_version", "2"},
                                         {"envoy.proxy_protocol_prefix", "test_stat_prefix"},
                                     }));
}

} // namespace Envoy
