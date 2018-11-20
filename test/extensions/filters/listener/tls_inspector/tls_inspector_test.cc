#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/tls_utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::AtLeast;
using testing::ElementsAre;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

using Envoy::Tls::Test::ClientHelloOptions;
;
using Envoy::Tls::Test::generateClientHello;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

class TlsInspectorTest : public testing::Test {
public:
  TlsInspectorTest() : cfg_(std::make_shared<Config>(store_)) {}

  void init() {
    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    filter_ = std::make_unique<Filter>(cfg_);
    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(42));

    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(
            DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
    filter_->onAccept(cb_);
  }

  void expectRecvClientHello(const std::vector<uint8_t>& client_hello) {
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(Invoke(
            [&client_hello](int, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
              ASSERT(length >= client_hello.size());
              memcpy(buffer, client_hello.data(), client_hello.size());
              return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
            }));
  }

  void readEventWithDirectContinue() {
    EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
    EXPECT_CALL(cb_, continueFilterChain(true));
    file_event_callback_(Event::FileReadyType::Read);
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Stats::IsolatedStoreImpl store_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  Network::MockListenerFilterCallbacks cb_;
  Network::MockConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
  Event::MockTimer* timer_{};
};

// Test that an exception is thrown for an invalid value for max_client_hello_size
TEST_F(TlsInspectorTest, MaxClientHelloSize) {
  EXPECT_THROW_WITH_MESSAGE(Config(store_, Config::TLS_MAX_CLIENT_HELLO + 1), EnvoyException,
                            "max_client_hello_size of 65537 is greater than maximum of 65536.");
}

// Test that the filter detects Closed events and terminates.
TEST_F(TlsInspectorTest, ConnectionClosed) {
  init();
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Closed);
  EXPECT_EQ(1, cfg_->stats().connection_closed_.value());
}

// Test that the filter detects timeout and terminates.
TEST_F(TlsInspectorTest, Timeout) {
  init();
  EXPECT_CALL(cb_, continueFilterChain(false));
  timer_->callback_();
  EXPECT_EQ(1, cfg_->stats().read_timeout_.value());
}

// Test that the filter detects detects read errors.
TEST_F(TlsInspectorTest, ReadError) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
    return Api::SysCallSizeResult{ssize_t(-1), ENOTSUP};
  }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
}

// Test that a ClientHello with an SNI value causes the correct name notification.
TEST_F(TlsInspectorTest, SniRegistered) {
  init();
  const std::string servername("example.com");
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setSniName(servername));
  expectRecvClientHello(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_));
  readEventWithDirectContinue();
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_not_found_.value());
}

// Test that a ClientHello with an ALPN value causes the correct name notification.
TEST_F(TlsInspectorTest, AlpnRegistered) {
  init();
  const std::vector<absl::string_view> alpn_protos = {absl::string_view("h2"),
                                                      absl::string_view("http/1.1")};
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setAlpn("\x02h2\x08http/1.1"));
  expectRecvClientHello(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_));
  readEventWithDirectContinue();
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_found_.value());
}

// Test that a ClientHello has default ECDSA ciphers detected.
TEST_F(TlsInspectorTest, DefaultEcdsaCiphers) {
  init();
  const std::vector<uint8_t> client_hello = generateClientHello(ClientHelloOptions());
  expectRecvClientHello(client_hello);
  // See https://www.iana.org/assignments/tls-parameters/tls-parameters.xml for
  // cipher suite IDs.
  EXPECT_CALL(socket_,
              setEcdsaCipherSuites(ElementsAre(0xc02b /* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */,
                                               0xc02c /* TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 */,
                                               0xcca9 /* TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305 */,
                                               0xc009 /* TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA */,
                                               0xc00a /* TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA */)));
  readEventWithDirectContinue();
}

// Test that a ClientHello has reduced ECDSA cipher suites when < TLSv1.2.
TEST_F(TlsInspectorTest, OlderEcdsaCiphersReduced) {
  init();
  std::vector<uint8_t> client_hello = generateClientHello(ClientHelloOptions().setNoTlsV1_2());
  // We do some ClientHello surgery here to force behaviors. Specifically, we
  // mutate the TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA cipher to unsupported
  // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256.
  ASSERT_EQ(0x0a, client_hello[51]);
  client_hello[51] = 0x2b;
  expectRecvClientHello(client_hello);
  // See https://www.iana.org/assignments/tls-parameters/tls-parameters.xml for
  // cipher suite IDs.
  EXPECT_CALL(socket_,
              setEcdsaCipherSuites(ElementsAre(0xc009 /* TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA */)));

  readEventWithDirectContinue();
}

// Test that a ClientHello with no ECDSA ciphers handles as expected.
TEST_F(TlsInspectorTest, NoEcdsaCiphers) {
  init();
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setCipherSuites("ECDHE-RSA-AES128-GCM-SHA256"));
  expectRecvClientHello(client_hello);
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_)).Times(0);
  readEventWithDirectContinue();
}

// Test that a ClientHello with only unknown ECDSA curves handles as expected.
TEST_F(TlsInspectorTest, UnknownEcdsaCurves) {
  init();
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setEcdhCurves("X25519"));
  expectRecvClientHello(client_hello);
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_)).Times(0);
  readEventWithDirectContinue();
}

// Test with the ClientHello spread over multiple socket reads.
TEST_F(TlsInspectorTest, MultipleReads) {
  init();
  const std::vector<absl::string_view> alpn_protos = {absl::string_view("h2")};
  const std::string servername("example.com");
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setSniName(servername).setAlpn("\x02h2"));
  {
    InSequence s;
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(InvokeWithoutArgs([]() -> Api::SysCallSizeResult {
          return Api::SysCallSizeResult{ssize_t(-1), EAGAIN};
        }));
    for (size_t i = 1; i <= client_hello.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke(
              [&client_hello, i](int, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= client_hello.size());
                memcpy(buffer, client_hello.data(), client_hello.size());
                return Api::SysCallSizeResult{ssize_t(i), 0};
              }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_found_.value());
}

// Test that the filter correctly handles a ClientHello with no extensions present.
TEST_F(TlsInspectorTest, NoExtensions) {
  init();
  const std::vector<uint8_t> client_hello = generateClientHello({});
  expectRecvClientHello(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setEcdsaCipherSuites(_));
  readEventWithDirectContinue();
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_not_found_.value());
}

// Test that the filter fails if the ClientHello is larger than the
// maximum allowed size.
TEST_F(TlsInspectorTest, ClientHelloTooBig) {
  const size_t max_size = 50;
  cfg_ = std::make_shared<Config>(store_, max_size);
  const std::vector<uint8_t> client_hello =
      generateClientHello(ClientHelloOptions().setSniName("example.com"));
  ASSERT(client_hello.size() > max_size);
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&client_hello](int, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == max_size);
            memcpy(buffer, client_hello.data(), length);
            return Api::SysCallSizeResult{ssize_t(length), 0};
          }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().client_hello_too_large_.value());
}

// Test that the filter fails on non-SSL data
TEST_F(TlsInspectorTest, NotSsl) {
  init();
  std::vector<uint8_t> data;

  // Use 100 bytes of zeroes. This is not valid as a ClientHello.
  data.resize(100);

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&data](int, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
        ASSERT(length >= data.size());
        memcpy(buffer, data.data(), data.size());
        return Api::SysCallSizeResult{ssize_t(data.size()), 0};
      }));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
