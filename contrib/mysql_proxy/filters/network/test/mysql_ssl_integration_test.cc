#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_command.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"
#include "openssl/evp.h"
#include "openssl/pem.h"

using testing::Ge;
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

// Client connection that supports mid-stream TLS upgrade (startTLS).
// Adapted from test/extensions/transport_sockets/starttls/starttls_integration_test.cc.
class StartTlsClientConnection : public Network::ClientConnectionImpl {
public:
  StartTlsClientConnection(Event::Dispatcher& dispatcher,
                           const Network::Address::InstanceConstSharedPtr& remote_address,
                           const Network::Address::InstanceConstSharedPtr& source_address,
                           Network::TransportSocketPtr&& transport_socket,
                           const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ClientConnectionImpl(dispatcher, remote_address, source_address,
                             std::move(transport_socket), options, nullptr) {}

  // Swap the raw transport socket for a TLS one and trigger the TLS handshake.
  void upgradeToTls(Network::TransportSocketPtr&& tls_socket) {
    transport_socket_ = std::move(tls_socket);
    transport_socket_->setTransportSocketCallbacks(*this);
    connecting_ = true;
    ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
};

class MySQLSSLIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public MySQLTestUtils,
                                public BaseIntegrationTest {
  std::string mysqlSslConfig() {
    return fmt::format(
        fmt::runtime(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/mysql_proxy/filters/network/test/mysql_ssl_require_test_config.yaml"))),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()),
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
  }

public:
  MySQLSSLIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlSslConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    EXPECT_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
        .WillOnce(
            Invoke([&](absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                       absl::AnyInvocable<void()> above_overflow) -> Buffer::Instance* {
              client_write_buffer_ = new NiceMock<MockWatermarkBuffer>(
                  std::move(below_low), std::move(above_high), std::move(above_overflow));
              ON_CALL(*client_write_buffer_, move(_))
                  .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
              ON_CALL(*client_write_buffer_, drain(_))
                  .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
              return client_write_buffer_;
            }))
        .WillOnce(
            Invoke([&](absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                       absl::AnyInvocable<void()> above_overflow) -> Buffer::Instance* {
              return new Buffer::WatermarkBuffer(std::move(below_low), std::move(above_high),
                                                 std::move(above_overflow));
            }));

    // Create raw buffer and TLS transport socket factories.
    auto raw_config =
        std::make_unique<envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer>();
    auto raw_factory =
        std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
    cleartext_context_ = Network::UpstreamTransportSocketFactoryPtr{
        raw_factory->createTransportSocketFactory(*raw_config, factory_context_).value()};

    tls_context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
    tls_context_ = Ssl::createClientSslTransportSocketFactory({}, *tls_context_manager_, *api_);

    payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);

    BaseIntegrationTest::initialize();

    // Create client connection with raw cleartext transport socket.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("listener_0"));
    conn_ = std::make_unique<StartTlsClientConnection>(
        *dispatcher_, address, Network::Address::InstanceConstSharedPtr(),
        cleartext_context_->createTransportSocket(nullptr, nullptr), nullptr);
    conn_->enableHalfClose(true);
    conn_->addConnectionCallbacks(connect_callbacks_);
    conn_->addReadFilter(payload_reader_);
  }

  // Upgrade the client connection to TLS.
  // Wait for the filter to process the SSL request and call startSecureTransport()
  // before initiating the client-side TLS handshake.
  void upgradeClientToTls() {
    test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
    conn_->upgradeToTls(tls_context_->createTransportSocket(
        std::make_shared<Network::TransportSocketOptionsImpl>(
            absl::string_view(""), std::vector<std::string>(), std::vector<std::string>()),
        nullptr));
    connect_callbacks_.reset();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    ASSERT_TRUE(connect_callbacks_.connected());
  }

  // Write data to client connection and wait for it to be sent.
  void clientWrite(const std::string& data) {
    uint64_t prev_drained = client_write_buffer_->bytesDrained();
    Buffer::OwnedImpl buf(data);
    conn_->write(buf, false);
    while (client_write_buffer_->bytesDrained() < prev_drained + data.length()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Wait for specific payload from server (via Envoy) on the client side.
  void clientWaitForData(const std::string& expected) {
    payload_reader_->setDataToWaitFor(expected);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  // Server greeting with caching_sha2_password and CLIENT_SSL.
  std::string encodeServerGreetingCachingSha2() {
    ServerGreeting greeting;
    greeting.setProtocol(MYSQL_PROTOCOL_10);
    greeting.setVersion(getVersion());
    greeting.setThreadId(MYSQL_THREAD_ID);
    greeting.setAuthPluginData(getAuthPluginData20());
    greeting.setServerCap(CLIENT_PLUGIN_AUTH | CLIENT_SECURE_CONNECTION | CLIENT_SSL);
    greeting.setServerCharset(MYSQL_SERVER_LANGUAGE);
    greeting.setServerStatus(MYSQL_SERVER_STATUS);
    greeting.setAuthPluginName("caching_sha2_password");
    Buffer::OwnedImpl buffer;
    greeting.encode(buffer);
    BufferHelper::encodeHdr(buffer, GREETING_SEQ_NUM);
    return buffer.toString();
  }

  std::string encodeAuthMoreDataPacket(const std::vector<uint8_t>& data, uint8_t seq) {
    AuthMoreMessage auth_more;
    auth_more.setAuthMoreData(data);
    Buffer::OwnedImpl buffer;
    auth_more.encode(buffer);
    BufferHelper::encodeHdr(buffer, seq);
    return buffer.toString();
  }

  std::string generateTestPemKey() {
    bssl::UniquePtr<EVP_PKEY_CTX> gen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
    EVP_PKEY_keygen_init(gen_ctx.get());
    EVP_PKEY_CTX_set_rsa_keygen_bits(gen_ctx.get(), 2048);
    EVP_PKEY* raw_pkey = nullptr;
    EVP_PKEY_keygen(gen_ctx.get(), &raw_pkey);
    bssl::UniquePtr<EVP_PKEY> pkey(raw_pkey);
    bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_PUBKEY(bio.get(), pkey.get());
    char* pem_data;
    long pem_len = BIO_get_mem_data(bio.get(), &pem_data);
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(pem_data, pem_len);
  }

  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::UpstreamTransportSocketFactoryPtr tls_context_;
  Network::UpstreamTransportSocketFactoryPtr cleartext_context_;
  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;
  std::unique_ptr<StartTlsClientConnection> conn_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MySQLSSLIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * caching_sha2_password fast auth (cache hit) with SSL termination.
 *
 * Client ←TLS→ Envoy ←plaintext→ FakeUpstream
 * greeting → SSL req → TLS upgrade → login → AuthMoreData(0x03) → OK
 */
TEST_P(MySQLSSLIntegrationTest, CachingSha2FastAuth) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // 1. Server greeting (cleartext, before TLS).
  std::string greeting = encodeServerGreetingCachingSha2();
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // 2. Client sends SSL request (cleartext).
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  clientWrite(ssl_req);
  // SSL request consumed by filter; filter calls startSecureTransport.

  // 3. Client upgrades to TLS.
  upgradeClientToTls();

  // 4. Client sends login over TLS (seq=2, rewritten to seq=1 for upstream).
  std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1);
  clientWrite(login);

  // Upstream receives the rewritten login in cleartext.
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // 5. Server responds with fast auth success (seq=2, rewritten to seq=3 for client).
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FAST_AUTH_SUCCESS}, CHALLENGE_RESP_SEQ_NUM)));

  // 6. Server sends OK (seq=3, rewritten to seq=4 for client).
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK, 0, 3)));

  // Wait for client to receive both responses.
  // Use a short sleep + non-block dispatch to let data flow through.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

/**
 * caching_sha2_password full auth (cache miss) with RSA mediation.
 *
 * greeting → SSL req → TLS → login → AuthMoreData(0x04) → client pw →
 * [filter: request-public-key → PEM key → RSA encrypt] → OK
 */
TEST_P(MySQLSSLIntegrationTest, CachingSha2FullAuthRsaMediation) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // 1. Server greeting.
  std::string greeting = encodeServerGreetingCachingSha2();
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // 2. SSL request + TLS upgrade.
  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();

  // 3. Client login over TLS.
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // 4. Server: full auth required (seq=2).
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));

  // Wait for client to receive AuthMoreData (filter forwards it).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // 5. Client sends cleartext password (seq=4 from client perspective).
  std::string pw_payload = std::string("testpass") + '\0';
  Buffer::OwnedImpl pw_pkt;
  BufferHelper::addString(pw_pkt, pw_payload);
  BufferHelper::encodeHdr(pw_pkt, 4);
  clientWrite(pw_pkt.toString());

  // 6. Filter intercepts password, sends request-public-key (0x02, seq=3) to upstream.
  size_t prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 5; },
      &upstream_data));

  // Verify request-public-key packet.
  std::string req_key = upstream_data.substr(prev_len);
  EXPECT_EQ(static_cast<uint8_t>(req_key[3]), 3u); // seq
  EXPECT_EQ(static_cast<uint8_t>(req_key[4]), MYSQL_REQUEST_PUBLIC_KEY);

  // 7. Server sends PEM public key (seq=4).
  std::string pem_key = generateTestPemKey();
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4)));

  // 8. Filter RSA-encrypts password and sends to upstream (seq=5, 256 bytes).
  prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 4 + 256; },
      &upstream_data));

  std::string enc_pkt = upstream_data.substr(prev_len);
  uint32_t enc_len = static_cast<uint8_t>(enc_pkt[0]) | (static_cast<uint8_t>(enc_pkt[1]) << 8) |
                     (static_cast<uint8_t>(enc_pkt[2]) << 16);
  EXPECT_EQ(enc_len, 256u);                        // RSA-2048 ciphertext
  EXPECT_EQ(static_cast<uint8_t>(enc_pkt[3]), 5u); // seq

  // 9. Server sends OK (raw seq=6, rewritten to seq=5 for client).
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK, 0, 6)));

  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

/**
 * Full auth — server rejects after RSA-encrypted password.
 */
TEST_P(MySQLSSLIntegrationTest, CachingSha2FullAuthRsaErr) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  std::string greeting = encodeServerGreetingCachingSha2();
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // Full auth required.
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Client password.
  Buffer::OwnedImpl pw_pkt;
  BufferHelper::addString(pw_pkt, std::string("wrongpw") + '\0');
  BufferHelper::encodeHdr(pw_pkt, 4);
  clientWrite(pw_pkt.toString());

  // Wait for request-public-key.
  size_t prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 5; },
      &upstream_data));

  // Send PEM key.
  std::string pem_key = generateTestPemKey();
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4)));

  // Wait for encrypted password.
  prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 4 + 256; },
      &upstream_data));

  // Server sends ERR (raw seq=6).
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_ERR, 0, 6)));

  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.login_failures", Ge(1));
}

/**
 * SSL Terminated: basic login with native password (no caching_sha2), then query.
 * Verifies seq rewriting works for the full lifecycle.
 */
TEST_P(MySQLSSLIntegrationTest, SslTerminateLoginThenQuery) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // Greeting (standard, not caching_sha2).
  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // SSL request + TLS upgrade.
  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();

  // Client login over TLS.
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // Server OK (seq=2, rewritten to seq=3 for client).
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK)));

  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Send a query (seq=0 after resetSeq).
  Command mysql_cmd{};
  mysql_cmd.setCmd(Command::Cmd::Query);
  mysql_cmd.setData("SELECT 1");
  Buffer::OwnedImpl query_buf;
  mysql_cmd.encode(query_buf);
  BufferHelper::encodeHdr(query_buf, 0);
  clientWrite(query_buf.toString());

  // Upstream should receive the query.
  size_t prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() > prev_len; }, &upstream_data));

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.queries_parsed", Ge(1));
}

/**
 * SSL Terminated: RSA mediation followed by query execution.
 * Full lifecycle: greeting → SSL → login → AuthMore(0x04) → RSA → OK → query.
 */
TEST_P(MySQLSSLIntegrationTest, CachingSha2FullAuthRsaThenQuery) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // Greeting + SSL + login.
  std::string greeting = encodeServerGreetingCachingSha2();
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // Full auth required → client password → RSA.
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  Buffer::OwnedImpl pw_pkt;
  BufferHelper::addString(pw_pkt, std::string("testpass") + '\0');
  BufferHelper::encodeHdr(pw_pkt, 4);
  clientWrite(pw_pkt.toString());

  // Wait for request-public-key.
  size_t prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 5; },
      &upstream_data));

  // Send PEM key + wait for encrypted password.
  std::string pem_key = generateTestPemKey();
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4)));
  prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 4 + 256; },
      &upstream_data));

  // Server OK (raw seq=6).
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK, 0, 6)));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Now send a query — verifies seq numbers are correct after RSA mediation.
  Command mysql_cmd{};
  mysql_cmd.setCmd(Command::Cmd::Query);
  mysql_cmd.setData("SELECT 1");
  Buffer::OwnedImpl query_buf;
  mysql_cmd.encode(query_buf);
  BufferHelper::encodeHdr(query_buf, 0);
  clientWrite(query_buf.toString());

  prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() > prev_len; }, &upstream_data));

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.queries_parsed", Ge(1));
}

// =============================================================================
// DISABLE mode integration tests — plain TCP proxy, no SSL termination.
// =============================================================================

class MySQLDisableIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public MySQLTestUtils,
                                    public BaseIntegrationTest {
  std::string mysqlConfig() {
    return fmt::format(
        fmt::runtime(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/mysql_proxy/filters/network/test/mysql_ssl_disable_test_config.yaml"))),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()));
  }

public:
  MySQLDisableIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void SetUp() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MySQLDisableIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * DISABLE mode: basic login, no SSL involved.
 */
TEST_P(MySQLDisableIntegrationTest, DisableBasicLogin) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // Greeting.
  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream->write(greeting));
  tcp_client->waitForData(greeting, true);

  // Client login (no SSL).
  std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM);
  ASSERT_TRUE(tcp_client->write(login));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData(login.length(), &upstream_data));
  EXPECT_EQ(login, upstream_data);

  // Server OK.
  std::string ok_resp = encodeClientLoginResp(MYSQL_RESP_OK);
  ASSERT_TRUE(fake_upstream->write(ok_resp));

  std::string downstream(greeting);
  downstream.append(ok_resp);
  tcp_client->waitForData(downstream, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

/**
 * DISABLE mode: SSL request passes through to upstream (passthrough).
 */
TEST_P(MySQLDisableIntegrationTest, DisableSslPassthrough) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // Greeting.
  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream->write(greeting));
  tcp_client->waitForData(greeting, true);

  // Client sends SSL request — in DISABLE mode, it passes through.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  ASSERT_TRUE(tcp_client->write(ssl_req));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData(ssl_req.length(), &upstream_data));
  // The SSL request should be forwarded unmodified.
  EXPECT_EQ(ssl_req, upstream_data);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.upgraded_to_ssl")->value(), 1);
}

// =============================================================================
// ALLOW mode integration tests — terminate SSL if client requests, accept cleartext.
// Uses the same StartTlsClientConnection as the REQUIRE tests.
// =============================================================================

class MySQLAllowIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public MySQLTestUtils,
                                  public BaseIntegrationTest {
  std::string mysqlSslConfig() {
    return fmt::format(
        fmt::runtime(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/mysql_proxy/filters/network/test/mysql_ssl_allow_test_config.yaml"))),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()),
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
  }

public:
  MySQLAllowIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlSslConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    EXPECT_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
        .WillOnce(
            Invoke([&](absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                       absl::AnyInvocable<void()> above_overflow) -> Buffer::Instance* {
              client_write_buffer_ = new NiceMock<MockWatermarkBuffer>(
                  std::move(below_low), std::move(above_high), std::move(above_overflow));
              ON_CALL(*client_write_buffer_, move(_))
                  .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
              ON_CALL(*client_write_buffer_, drain(_))
                  .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
              return client_write_buffer_;
            }))
        .WillRepeatedly(
            Invoke([](absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                      absl::AnyInvocable<void()> above_overflow) -> Buffer::Instance* {
              return new Buffer::WatermarkBuffer(std::move(below_low), std::move(above_high),
                                                 std::move(above_overflow));
            }));

    auto raw_config =
        std::make_unique<envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer>();
    auto raw_factory =
        std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
    cleartext_context_ = Network::UpstreamTransportSocketFactoryPtr{
        raw_factory->createTransportSocketFactory(*raw_config, factory_context_).value()};

    tls_context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
    tls_context_ = Ssl::createClientSslTransportSocketFactory({}, *tls_context_manager_, *api_);

    payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);

    BaseIntegrationTest::initialize();

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("listener_0"));
    conn_ = std::make_unique<StartTlsClientConnection>(
        *dispatcher_, address, Network::Address::InstanceConstSharedPtr(),
        cleartext_context_->createTransportSocket(nullptr, nullptr), nullptr);
    conn_->enableHalfClose(true);
    conn_->addConnectionCallbacks(connect_callbacks_);
    conn_->addReadFilter(payload_reader_);
  }

  void upgradeClientToTls() {
    test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
    conn_->upgradeToTls(tls_context_->createTransportSocket(
        std::make_shared<Network::TransportSocketOptionsImpl>(
            absl::string_view(""), std::vector<std::string>(), std::vector<std::string>()),
        nullptr));
    connect_callbacks_.reset();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    ASSERT_TRUE(connect_callbacks_.connected());
  }

  void clientWrite(const std::string& data) {
    uint64_t prev_drained = client_write_buffer_->bytesDrained();
    Buffer::OwnedImpl buf(data);
    conn_->write(buf, false);
    while (client_write_buffer_->bytesDrained() < prev_drained + data.length()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void clientWaitForData(const std::string& expected) {
    payload_reader_->setDataToWaitFor(expected);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  std::string encodeServerGreetingCachingSha2() {
    ServerGreeting greeting;
    greeting.setProtocol(MYSQL_PROTOCOL_10);
    greeting.setVersion(getVersion());
    greeting.setThreadId(MYSQL_THREAD_ID);
    greeting.setAuthPluginData(getAuthPluginData20());
    greeting.setServerCap(CLIENT_PLUGIN_AUTH | CLIENT_SECURE_CONNECTION | CLIENT_SSL);
    greeting.setServerCharset(MYSQL_SERVER_LANGUAGE);
    greeting.setServerStatus(MYSQL_SERVER_STATUS);
    greeting.setAuthPluginName("caching_sha2_password");
    Buffer::OwnedImpl buffer;
    greeting.encode(buffer);
    BufferHelper::encodeHdr(buffer, GREETING_SEQ_NUM);
    return buffer.toString();
  }

  std::string encodeAuthMoreDataPacket(const std::vector<uint8_t>& data, uint8_t seq) {
    AuthMoreMessage auth_more;
    auth_more.setAuthMoreData(data);
    Buffer::OwnedImpl buffer;
    auth_more.encode(buffer);
    BufferHelper::encodeHdr(buffer, seq);
    return buffer.toString();
  }

  std::string generateTestPemKey() {
    bssl::UniquePtr<EVP_PKEY_CTX> gen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
    EVP_PKEY_keygen_init(gen_ctx.get());
    EVP_PKEY_CTX_set_rsa_keygen_bits(gen_ctx.get(), 2048);
    EVP_PKEY* raw_pkey = nullptr;
    EVP_PKEY_keygen(gen_ctx.get(), &raw_pkey);
    bssl::UniquePtr<EVP_PKEY> pkey(raw_pkey);
    bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_PUBKEY(bio.get(), pkey.get());
    char* pem_data;
    long pem_len = BIO_get_mem_data(bio.get(), &pem_data);
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(pem_data, pem_len);
  }

  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::UpstreamTransportSocketFactoryPtr tls_context_;
  Network::UpstreamTransportSocketFactoryPtr cleartext_context_;
  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;
  std::unique_ptr<StartTlsClientConnection> conn_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MySQLAllowIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * ALLOW mode: SSL client connects, terminates SSL, basic login OK.
 */
TEST_P(MySQLAllowIntegrationTest, AllowSslClientLogin) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // SSL request + TLS upgrade — accepted in ALLOW mode.
  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();

  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK)));

  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
}

/**
 * ALLOW mode: non-SSL client connects in cleartext, login OK.
 */
TEST_P(MySQLAllowIntegrationTest, AllowNonSslClientLogin) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // Client sends login directly (no SSL) — accepted in ALLOW mode.
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK)));

  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.upgraded_to_ssl")->value(), 0);
}

/**
 * ALLOW mode: SSL client with caching_sha2 full auth (RSA mediation).
 */
TEST_P(MySQLAllowIntegrationTest, AllowSslFullAuthRsaMediation) {
  initialize();
  conn_->connect();

  FakeRawConnectionPtr fake_upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream));

  // Greeting with caching_sha2_password.
  std::string greeting = encodeServerGreetingCachingSha2();
  ASSERT_TRUE(fake_upstream->write(greeting));
  clientWaitForData(greeting);

  // SSL + login.
  clientWrite(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM));
  upgradeClientToTls();
  clientWrite(encodeClientLogin(CLIENT_PROTOCOL_41, "testuser", CHALLENGE_SEQ_NUM + 1));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream->waitForData([](const std::string& data) { return !data.empty(); },
                                         &upstream_data));

  // Full auth required.
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Client password.
  Buffer::OwnedImpl pw_pkt;
  BufferHelper::addString(pw_pkt, std::string("testpass") + '\0');
  BufferHelper::encodeHdr(pw_pkt, 4);
  clientWrite(pw_pkt.toString());

  // Filter should inject request-public-key (0x02, seq=3).
  size_t prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 5; },
      &upstream_data));

  std::string req_key = upstream_data.substr(prev_len);
  EXPECT_EQ(static_cast<uint8_t>(req_key[3]), 3u);
  EXPECT_EQ(static_cast<uint8_t>(req_key[4]), MYSQL_REQUEST_PUBLIC_KEY);

  // Send PEM key.
  std::string pem_key = generateTestPemKey();
  ASSERT_TRUE(fake_upstream->write(
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4)));

  // Wait for encrypted password (256 bytes).
  prev_len = upstream_data.length();
  ASSERT_TRUE(fake_upstream->waitForData(
      [prev_len](const std::string& data) { return data.length() >= prev_len + 4 + 256; },
      &upstream_data));

  // Server OK.
  ASSERT_TRUE(fake_upstream->write(encodeClientLoginResp(MYSQL_RESP_OK, 0, 6)));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream->waitForDisconnect());

  test_server_->waitForCounter("mysql.mysql_stats.upgraded_to_ssl", Ge(1));
  test_server_->waitForCounter("mysql.mysql_stats.login_attempts", Ge(1));
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
