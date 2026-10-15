#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

constexpr char kRustModulesPath[] =
    "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust";
constexpr char kReferenceModule[] = "transport_socket_integration_test";
constexpr char kSpliceModule[] = "transport_socket_splice";
constexpr uint8_t kXorKey = 0x5a;

std::string xorString(absl::string_view in, uint8_t key) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    out.push_back(static_cast<char>(static_cast<uint8_t>(c) ^ key));
  }
  return out;
}

// Bytes that depend on their offset and on the seed, so a reordered, truncated, or mixed-up stream
// does not compare equal.
std::string patternPayload(size_t size, uint8_t seed) {
  std::string payload(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    payload[i] = static_cast<char>((i * 31 + seed) % 251);
  }
  return payload;
}

// Compares payloads without printing megabytes on a mismatch.
testing::AssertionResult samePayload(absl::string_view expected, absl::string_view actual) {
  if (expected == actual) {
    return testing::AssertionSuccess();
  }
  const size_t common = std::min(expected.size(), actual.size());
  size_t offset = 0;
  while (offset < common && expected[offset] == actual[offset]) {
    ++offset;
  }
  return testing::AssertionFailure() << "payloads differ at offset " << offset << ", expected "
                                     << expected.size() << " bytes, got " << actual.size();
}

class DynamicModuleTransportSocketIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  DynamicModuleTransportSocketIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

protected:
  static envoy::config::core::v3::TransportSocket
  buildTransportSocket(const std::string& name, const std::string& module = kReferenceModule) {
    envoy::config::core::v3::TransportSocket transport_socket;
    transport_socket.set_name("envoy.transport_sockets.dynamic_modules");
    envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket config;
    config.mutable_dynamic_module_config()->set_name(module);
    config.set_transport_socket_name(name);
    std::ignore = transport_socket.mutable_typed_config()->PackFrom(config);
    return transport_socket;
  }

  void initializeWithDownstreamSocket(const std::string& name) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    config_helper_.addConfigModifier([name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      *filter_chain->mutable_transport_socket() = buildTransportSocket(name);
    });
    BaseIntegrationTest::initialize();
  }

  void initializeWithUpstreamSocket(const std::string& name) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    config_helper_.addConfigModifier([name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      *cluster->mutable_transport_socket() = buildTransportSocket(name);
    });
    BaseIntegrationTest::initialize();
  }

  // Puts the splice transport socket on the listener and, if `splice_upstream` is set, on the
  // cluster. `name` selects the module-owned relay ("splice") or the engine pump ("engine_splice").
  void initializeWithSplice(bool splice_upstream = true, const std::string& name = "splice") {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    config_helper_.addConfigModifier(
        [splice_upstream, name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* static_resources = bootstrap.mutable_static_resources();
          *static_resources->mutable_listeners(0)
               ->mutable_filter_chains(0)
               ->mutable_transport_socket() = buildTransportSocket(name, kSpliceModule);
          if (splice_upstream) {
            *static_resources->mutable_clusters(0)->mutable_transport_socket() =
                buildTransportSocket(name, kSpliceModule);
          }
        });
    BaseIntegrationTest::initialize();
  }

  // Connects a client and passes a first chunk each way, which goes through Envoy's buffers. A
  // splice socket pairs with its upstream on that exchange and splices whatever follows.
  void connectAndExchangeGreetings() {
    tcp_client_ = makeTcpConnection(lookupPort("listener_0"));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));
    ASSERT_TRUE(tcp_client_->write("hello", false));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5));
    ASSERT_TRUE(fake_upstream_connection_->write("world"));
    tcp_client_->waitForData("world");
  }

  // Sends `upload` and `download` after the greetings and checks that each side got every byte in
  // order.
  void transferAfterGreetings(const std::string& upload, const std::string& download) {
    ASSERT_TRUE(tcp_client_->write(upload, false));
    std::string upstream_data;
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5 + upload.size(), &upstream_data));
    EXPECT_TRUE(samePayload("hello" + upload, upstream_data));
    ASSERT_TRUE(fake_upstream_connection_->write(download));
    ASSERT_TRUE(tcp_client_->waitForData(5 + download.size()));
    EXPECT_TRUE(samePayload("world" + download, tcp_client_->data()));
  }

  // Half-closes the downstream first, then the upstream, and waits for the session to end.
  void closeGracefully() {
    ASSERT_TRUE(tcp_client_->write("", true));
    ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
    ASSERT_TRUE(fake_upstream_connection_->write("", true));
    tcp_client_->waitForHalfClose();
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    tcp_client_->close();
  }

  IntegrationTcpClientPtr tcp_client_;
  FakeRawConnectionPtr fake_upstream_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleTransportSocketIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The passthrough socket forwards bytes unchanged in both directions.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamPassthrough) {
  initializeWithDownstreamSocket("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ("hello", upstream_data);

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// The XOR socket transforms bytes in both directions on the downstream connection.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamXorTransform) {
  initializeWithDownstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The downstream read path decrypts client bytes before they reach the upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ(xorString("hello", kXorKey), upstream_data);

  // The downstream write path encrypts upstream bytes before they reach the client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData(xorString("world", kXorKey));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// The XOR socket transforms bytes in both directions on the upstream connection.
TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamXorTransform) {
  initializeWithUpstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The upstream write path encrypts client bytes before they reach the upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ(xorString("hello", kXorKey), upstream_data);

  // The upstream read path decrypts upstream bytes before they reach the client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData(xorString("world", kXorKey));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// Large transfers exercise the read and write loops across multiple I/O operations, including the
// write buffer length check.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamLargeData) {
  initializeWithDownstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  const std::string payload(100000, 'x');
  ASSERT_TRUE(tcp_client->write(payload, true));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(payload.size(), &upstream_data));
  EXPECT_EQ(xorString(payload, kXorKey), upstream_data);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// A downstream half-close right behind spliced bytes reaches the upstream only after all of them.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceDownstreamHalfCloseFollowsSplicedBytes) {
  initializeWithSplice();
  connectAndExchangeGreetings();

  const std::string upload = patternPayload(8 << 20, 3);
  std::string upstream_data;
  EXPECT_LOG_CONTAINS("debug", "splicing downstream to upstream", {
    ASSERT_TRUE(tcp_client_->write(upload, true));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5 + upload.size(), &upstream_data));
  });
  EXPECT_TRUE(samePayload("hello" + upload, upstream_data));
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());

  // The other direction keeps working after the half-close.
  const std::string download = patternPayload(1 << 20, 4);
  ASSERT_TRUE(fake_upstream_connection_->write(download, true));
  ASSERT_TRUE(tcp_client_->waitForData(5 + download.size()));
  EXPECT_TRUE(samePayload("world" + download, tcp_client_->data()));
  tcp_client_->waitForHalfClose();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_->close();
}

// The upstream may speak first, as in server-first protocols. Its first bytes pass through Envoy
// and the rest is spliced.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceUpstreamSpeaksFirst) {
  initializeWithSplice();
  tcp_client_ = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  const std::string download = patternPayload(4 << 20, 7);
  EXPECT_LOG_CONTAINS("debug", "splicing upstream to downstream", {
    ASSERT_TRUE(fake_upstream_connection_->write("greeting"));
    tcp_client_->waitForData("greeting");
    ASSERT_TRUE(fake_upstream_connection_->write(download));
    ASSERT_TRUE(tcp_client_->waitForData(8 + download.size()));
  });
  EXPECT_TRUE(samePayload("greeting" + download, tcp_client_->data()));

  ASSERT_TRUE(tcp_client_->write("bye", true));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(3));
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection_->close());
  tcp_client_->waitForHalfClose();
  tcp_client_->close();
}

// Spliced bytes count in the access log and the connection stats exactly like buffered bytes.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceCountsSplicedBytes) {
  useListenerAccessLog("BYTES_RECEIVED=%BYTES_RECEIVED% BYTES_SENT=%BYTES_SENT% "
                       "DOWNSTREAM_WIRE_BYTES_RECEIVED=%DOWNSTREAM_WIRE_BYTES_RECEIVED% "
                       "DOWNSTREAM_WIRE_BYTES_SENT=%DOWNSTREAM_WIRE_BYTES_SENT% "
                       "UPSTREAM_WIRE_BYTES_SENT=%UPSTREAM_WIRE_BYTES_SENT% "
                       "UPSTREAM_WIRE_BYTES_RECEIVED=%UPSTREAM_WIRE_BYTES_RECEIVED%");
  initializeWithSplice();
  const std::string upload = patternPayload(3 << 20, 8);
  const std::string download = patternPayload(5 << 20, 9);
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"debug", "paired with upstream connection"},
                                  {"debug", "splicing downstream to upstream"},
                                  {"debug", "splicing upstream to downstream"}}),
      {
        connectAndExchangeGreetings();
        transferAfterGreetings(upload, download);
      });
  closeGracefully();

  const uint64_t received = 5 + upload.size();
  const uint64_t sent = 5 + download.size();
  test_server_->waitForCounter("tcp.tcpproxy_stats.downstream_cx_rx_bytes_total",
                               testing::Eq(received));
  test_server_->waitForCounter("tcp.tcpproxy_stats.downstream_cx_tx_bytes_total",
                               testing::Eq(sent));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_tx_bytes_total",
                               testing::Eq(received));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_rx_bytes_total", testing::Eq(sent));
  test_server_.reset();
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr(fmt::format(
                  "BYTES_RECEIVED={0} BYTES_SENT={1} DOWNSTREAM_WIRE_BYTES_RECEIVED={0} "
                  "DOWNSTREAM_WIRE_BYTES_SENT={1} UPSTREAM_WIRE_BYTES_SENT={0} "
                  "UPSTREAM_WIRE_BYTES_RECEIVED={1}",
                  received, sent)));
}

// Concurrent sessions each splice their own bytes and never those of another session.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceKeepsConcurrentSessionsApart) {
  initializeWithSplice();
  constexpr int kSessions = 6;
  std::vector<IntegrationTcpClientPtr> clients;
  std::vector<FakeRawConnectionPtr> upstreams;
  EXPECT_LOG_CONTAINS_N_TIMES("debug", "paired with upstream connection", kSessions, {
    for (int i = 0; i < kSessions; ++i) {
      clients.push_back(makeTcpConnection(lookupPort("listener_0")));
      FakeRawConnectionPtr upstream;
      ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));
      upstreams.push_back(std::move(upstream));
    }
    // The first chunk of each session names it, which pins the client to its upstream connection.
    for (int i = 0; i < kSessions; ++i) {
      const std::string tag = absl::StrCat("session", i);
      ASSERT_TRUE(clients[i]->write(tag, false));
      std::string upstream_data;
      ASSERT_TRUE(upstreams[i]->waitForData(tag.size(), &upstream_data));
      ASSERT_EQ(tag, upstream_data);
      ASSERT_TRUE(upstreams[i]->write(tag));
      clients[i]->waitForData(tag);
    }
  });
  EXPECT_LOG_CONTAINS_N_TIMES("debug", "splicing", 2 * kSessions, {
    for (int i = 0; i < kSessions; ++i) {
      ASSERT_TRUE(clients[i]->write(patternPayload(1 << 20, 10 + i), false));
      ASSERT_TRUE(upstreams[i]->write(patternPayload(1 << 20, 20 + i)));
    }
    for (int i = 0; i < kSessions; ++i) {
      const std::string tag = absl::StrCat("session", i);
      std::string upstream_data;
      ASSERT_TRUE(upstreams[i]->waitForData(tag.size() + (1 << 20), &upstream_data));
      EXPECT_TRUE(samePayload(tag + patternPayload(1 << 20, 10 + i), upstream_data));
      ASSERT_TRUE(clients[i]->waitForData(tag.size() + (1 << 20)));
      EXPECT_TRUE(samePayload(tag + patternPayload(1 << 20, 20 + i), clients[i]->data()));
    }
  });
  for (int i = 0; i < kSessions; ++i) {
    ASSERT_TRUE(clients[i]->write("", true));
    ASSERT_TRUE(upstreams[i]->waitForHalfClose());
    ASSERT_TRUE(upstreams[i]->write("", true));
    clients[i]->waitForHalfClose();
    ASSERT_TRUE(upstreams[i]->waitForDisconnect());
    clients[i]->close();
  }
}

// A downstream splice socket whose upstream is not a splice socket finds nothing to pair with and
// keeps copying through Envoy.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceFallsBackWithoutUpstreamSpliceSocket) {
  initializeWithSplice(false);
  EXPECT_LOG_CONTAINS("debug", "cannot be spliced", { connectAndExchangeGreetings(); });
  EXPECT_LOG_NOT_CONTAINS("debug", "splicing", {
    transferAfterGreetings(patternPayload(2 << 20, 11), patternPayload(2 << 20, 12));
  });
  closeGracefully();
}

// The engine pump takes over both fds at the clean boundary and relays both directions in kernel.
TEST_P(DynamicModuleTransportSocketIntegrationTest, EngineSpliceRelaysBothDirections) {
  initializeWithSplice(true, "engine_splice");
  EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
    connectAndExchangeGreetings();
    transferAfterGreetings(patternPayload(8 << 20, 21), patternPayload(8 << 20, 22));
  });
  closeGracefully();
}

// Each engine-pump session relays only its own bytes.
TEST_P(DynamicModuleTransportSocketIntegrationTest, EngineSpliceKeepsConcurrentSessionsApart) {
  initializeWithSplice(true, "engine_splice");
  constexpr int kSessions = 4;
  std::vector<IntegrationTcpClientPtr> clients;
  std::vector<FakeRawConnectionPtr> upstreams;
  for (int i = 0; i < kSessions; ++i) {
    clients.push_back(makeTcpConnection(lookupPort("listener_0")));
    FakeRawConnectionPtr upstream;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));
    upstreams.push_back(std::move(upstream));
  }
  // The first chunk of each session names it, which pins the client to its upstream connection.
  for (int i = 0; i < kSessions; ++i) {
    const std::string tag = absl::StrCat("session", i);
    ASSERT_TRUE(clients[i]->write(tag, false));
    std::string upstream_data;
    ASSERT_TRUE(upstreams[i]->waitForData(tag.size(), &upstream_data));
    ASSERT_EQ(tag, upstream_data);
    ASSERT_TRUE(upstreams[i]->write(tag));
    clients[i]->waitForData(tag);
  }
  EXPECT_LOG_CONTAINS_N_TIMES("debug", "splice pump armed", kSessions, {
    for (int i = 0; i < kSessions; ++i) {
      ASSERT_TRUE(clients[i]->write(patternPayload(1 << 20, 30 + i), false));
      ASSERT_TRUE(upstreams[i]->write(patternPayload(1 << 20, 40 + i)));
    }
    for (int i = 0; i < kSessions; ++i) {
      const std::string tag = absl::StrCat("session", i);
      std::string upstream_data;
      ASSERT_TRUE(upstreams[i]->waitForData(tag.size() + (1 << 20), &upstream_data));
      EXPECT_TRUE(samePayload(tag + patternPayload(1 << 20, 30 + i), upstream_data));
      ASSERT_TRUE(clients[i]->waitForData(tag.size() + (1 << 20)));
      EXPECT_TRUE(samePayload(tag + patternPayload(1 << 20, 40 + i), clients[i]->data()));
    }
  });
  for (int i = 0; i < kSessions; ++i) {
    ASSERT_TRUE(clients[i]->write("", true));
    ASSERT_TRUE(upstreams[i]->waitForHalfClose());
    ASSERT_TRUE(upstreams[i]->write("", true));
    clients[i]->waitForHalfClose();
    ASSERT_TRUE(upstreams[i]->waitForDisconnect());
    clients[i]->close();
  }
}

// The engine pump forwards a downstream half-close and still relays the response the upstream sends
// afterwards.
TEST_P(DynamicModuleTransportSocketIntegrationTest,
       EngineSpliceUpstreamAnswersAfterDownstreamHalfClose) {
  initializeWithSplice(true, "engine_splice");
  connectAndExchangeGreetings();
  const std::string request = patternPayload(1 << 20, 23);
  EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
    ASSERT_TRUE(tcp_client_->write(request, true));
    std::string upstream_data;
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5 + request.size(), &upstream_data));
    EXPECT_TRUE(samePayload("hello" + request, upstream_data));
  });
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());

  const std::string response = patternPayload(1 << 20, 24);
  ASSERT_TRUE(fake_upstream_connection_->write(response, true));
  ASSERT_TRUE(tcp_client_->waitForData(5 + response.size()));
  EXPECT_TRUE(samePayload("world" + response, tcp_client_->data()));
  tcp_client_->waitForHalfClose();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_->close();
}

// Bytes the engine pump relays count in the access log like bytes relayed through Envoy.
TEST_P(DynamicModuleTransportSocketIntegrationTest, EngineSpliceCountsSplicedBytes) {
  useListenerAccessLog("BYTES_RECEIVED=%BYTES_RECEIVED% BYTES_SENT=%BYTES_SENT% "
                       "DOWNSTREAM_WIRE_BYTES_RECEIVED=%DOWNSTREAM_WIRE_BYTES_RECEIVED% "
                       "DOWNSTREAM_WIRE_BYTES_SENT=%DOWNSTREAM_WIRE_BYTES_SENT% "
                       "UPSTREAM_WIRE_BYTES_SENT=%UPSTREAM_WIRE_BYTES_SENT% "
                       "UPSTREAM_WIRE_BYTES_RECEIVED=%UPSTREAM_WIRE_BYTES_RECEIVED%");
  initializeWithSplice(true, "engine_splice");
  const std::string upload = patternPayload(3 << 20, 25);
  const std::string download = patternPayload(5 << 20, 26);
  EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
    connectAndExchangeGreetings();
    transferAfterGreetings(upload, download);
  });
  closeGracefully();
  test_server_.reset();
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr(fmt::format(
                  "BYTES_RECEIVED={0} BYTES_SENT={1} DOWNSTREAM_WIRE_BYTES_RECEIVED={0} "
                  "DOWNSTREAM_WIRE_BYTES_SENT={1} UPSTREAM_WIRE_BYTES_SENT={0} "
                  "UPSTREAM_WIRE_BYTES_RECEIVED={1}",
                  5 + upload.size(), 5 + download.size())));
}

// A connection closed outside the pump, here by the TCP proxy idle timeout, stops the session and
// closes the other connection. The worker still serves a new session afterwards.
TEST_P(DynamicModuleTransportSocketIntegrationTest, EngineSpliceStopsOnExternalClose) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* config_blob = bootstrap.mutable_static_resources()
                            ->mutable_listeners(0)
                            ->mutable_filter_chains(0)
                            ->mutable_filters(0)
                            ->mutable_typed_config();
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    // Bytes the pump relays do not reset the idle timer, so it fires on an engaged session.
    tcp_proxy_config.mutable_idle_timeout()->set_seconds(1);
    std::ignore = config_blob->PackFrom(tcp_proxy_config);
  });
  initializeWithSplice(true, "engine_splice");
  for (int i = 0; i < 2; ++i) {
    EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
      connectAndExchangeGreetings();
      transferAfterGreetings(patternPayload(64 << 10, 29 + i), patternPayload(64 << 10, 31 + i));
    });
    // Envoy closes both connections, which each peer sees as an end of stream.
    tcp_client_->waitForHalfClose();
    ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
    test_server_->waitForCounter("tcp.tcpproxy_stats.idle_timeout", testing::Eq(i + 1u));
    tcp_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
  }
}

#if ENVOY_PLATFORM_ENABLE_SEND_RST
// A downstream reset on a session that splices its bytes reaches the upstream as a reset.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceDownstreamResetPropagatesToUpstream) {
  initializeWithSplice();
  connectAndExchangeGreetings();
  EXPECT_LOG_CONTAINS("debug", "splicing downstream to upstream", {
    ASSERT_TRUE(tcp_client_->write(patternPayload(1 << 20, 15), false));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5 + (1 << 20)));
  });
  tcp_client_->close(Network::ConnectionCloseType::AbortReset);
  ASSERT_TRUE(fake_upstream_connection_->waitForRstDisconnect());
}

// An upstream reset on a session that splices its bytes reaches the downstream as a reset.
TEST_P(DynamicModuleTransportSocketIntegrationTest, SpliceUpstreamResetPropagatesToDownstream) {
  initializeWithSplice();
  connectAndExchangeGreetings();
  const std::string download = patternPayload(1 << 20, 17);
  EXPECT_LOG_CONTAINS("debug", "splicing upstream to downstream", {
    ASSERT_TRUE(fake_upstream_connection_->write(download));
    ASSERT_TRUE(tcp_client_->waitForData(5 + download.size()));
  });
  ASSERT_TRUE(fake_upstream_connection_->close(Network::ConnectionCloseType::AbortReset));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_->waitForDisconnect();
  EXPECT_EQ(StreamInfo::DetectedCloseType::RemoteReset,
            tcp_client_->connection()->detectedCloseType());
}

// A downstream reset on a session the engine pump owns reaches the upstream as a reset.
TEST_P(DynamicModuleTransportSocketIntegrationTest,
       EngineSpliceDownstreamResetPropagatesToUpstream) {
  initializeWithSplice(true, "engine_splice");
  connectAndExchangeGreetings();
  EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
    ASSERT_TRUE(tcp_client_->write(patternPayload(1 << 20, 27), false));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(5 + (1 << 20)));
  });
  tcp_client_->close(Network::ConnectionCloseType::AbortReset);
  ASSERT_TRUE(fake_upstream_connection_->waitForRstDisconnect());
}

// An upstream reset on a session the engine pump owns reaches the downstream as a reset.
TEST_P(DynamicModuleTransportSocketIntegrationTest,
       EngineSpliceUpstreamResetPropagatesToDownstream) {
  initializeWithSplice(true, "engine_splice");
  connectAndExchangeGreetings();
  const std::string download = patternPayload(1 << 20, 28);
  EXPECT_LOG_CONTAINS("debug", "splice pump armed", {
    ASSERT_TRUE(fake_upstream_connection_->write(download));
    ASSERT_TRUE(tcp_client_->waitForData(5 + download.size()));
  });
  ASSERT_TRUE(fake_upstream_connection_->close(Network::ConnectionCloseType::AbortReset));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_->waitForDisconnect();
  EXPECT_EQ(StreamInfo::DetectedCloseType::RemoteReset,
            tcp_client_->connection()->detectedCloseType());
}

// A downstream reset on the buffered path also reaches the upstream as a reset. The module only
// sees a failed read and closes, and Envoy reports the close as a reset.
TEST_P(DynamicModuleTransportSocketIntegrationTest, BufferedResetPropagatesToUpstream) {
  initializeWithSplice(false);
  connectAndExchangeGreetings();
  tcp_client_->close(Network::ConnectionCloseType::AbortReset);
  ASSERT_TRUE(fake_upstream_connection_->waitForRstDisconnect());
}
#endif

} // namespace
} // namespace Envoy
