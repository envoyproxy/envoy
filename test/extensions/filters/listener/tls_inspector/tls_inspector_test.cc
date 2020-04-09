#include "common/network/io_socket_handle_impl.h"

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {
namespace {

class TlsInspectorTest : public testing::TestWithParam<std::tuple<uint16_t, uint16_t>> {
public:
  TlsInspectorTest()
      : cfg_(std::make_shared<Config>(store_)),
        io_handle_(std::make_unique<Network::IoSocketHandleImpl>(42)) {}
  ~TlsInspectorTest() override { io_handle_->close(); }

  void init() {
    filter_ = std::make_unique<Filter>(cfg_);

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));

    // Prepare the first recv attempt during
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(
            Invoke([](os_fd_t fd, void* buffer, size_t length, int flag) -> Api::SysCallSizeResult {
              ENVOY_LOG_MISC(error, "In mock syscall recv {} {} {} {}", fd, buffer, length, flag);
              return Api::SysCallSizeResult{static_cast<ssize_t>(0), 0};
            }));
    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(
            DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
    filter_->onAccept(cb_);
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
  Network::IoHandlePtr io_handle_;
};

INSTANTIATE_TEST_SUITE_P(TlsProtocolVersions, TlsInspectorTest,
                         testing::Values(std::make_tuple(Config::TLS_MIN_SUPPORTED_VERSION,
                                                         Config::TLS_MAX_SUPPORTED_VERSION),
                                         std::make_tuple(TLS1_VERSION, TLS1_VERSION),
                                         std::make_tuple(TLS1_1_VERSION, TLS1_1_VERSION),
                                         std::make_tuple(TLS1_2_VERSION, TLS1_2_VERSION),
                                         std::make_tuple(TLS1_3_VERSION, TLS1_3_VERSION)));

// Test that an exception is thrown for an invalid value for max_client_hello_size
TEST_P(TlsInspectorTest, MaxClientHelloSize) {
  EXPECT_THROW_WITH_MESSAGE(Config(store_, Config::TLS_MAX_CLIENT_HELLO + 1), EnvoyException,
                            "max_client_hello_size of 65537 is greater than maximum of 65536.");
}

// Test that the filter detects Closed events and terminates.
TEST_P(TlsInspectorTest, ConnectionClosed) {
  init();
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Closed);
  EXPECT_EQ(1, cfg_->stats().connection_closed_.value());
}

// Test that the filter detects detects read errors.
TEST_P(TlsInspectorTest, ReadError) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
    return Api::SysCallSizeResult{ssize_t(-1), ENOTSUP};
  }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().read_error_.value());
}

// Test that a ClientHello with an SNI value causes the correct name notification.
TEST_P(TlsInspectorTest, SniRegistered) {
  init();
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "");
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_not_found_.value());
}

// Test that a ClientHello with an ALPN value causes the correct name notification.
TEST_P(TlsInspectorTest, AlpnRegistered) {
  init();
  const std::vector<absl::string_view> alpn_protos = {absl::string_view("h2"),
                                                      absl::string_view("http/1.1")};
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), "", "\x02h2\x08http/1.1");
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_found_.value());
}

// Test with the ClientHello spread over multiple socket reads.
TEST_P(TlsInspectorTest, MultipleReads) {
  init();
  const std::vector<absl::string_view> alpn_protos = {absl::string_view("h2")};
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "\x02h2");
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
TEST_P(TlsInspectorTest, NoExtensions) {
  init();
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), "", "");
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_not_found_.value());
}

// Test that the filter fails if the ClientHello is larger than the
// maximum allowed size.
TEST_P(TlsInspectorTest, ClientHelloTooBig) {
  const size_t max_size = 50;
  cfg_ = std::make_shared<Config>(store_, static_cast<uint32_t>(max_size));
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), "example.com", "");
  ASSERT(client_hello.size() > max_size);
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [=, &client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length == max_size);
            memcpy(buffer, client_hello.data(), length);
            return Api::SysCallSizeResult{ssize_t(length), 0};
          }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().client_hello_too_large_.value());
}

// Test that the filter fails on non-SSL data
TEST_P(TlsInspectorTest, NotSsl) {
  init();
  std::vector<uint8_t> data;

  // Use 100 bytes of zeroes. This is not valid as a ClientHello.
  data.resize(100);

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(
          Invoke([&data](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= data.size());
            memcpy(buffer, data.data(), data.size());
            return Api::SysCallSizeResult{ssize_t(data.size()), 0};
          }));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
}

TEST_P(TlsInspectorTest, InlineReadSucceed) {
  filter_ = std::make_unique<Filter>(cfg_);

  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
  const std::vector<absl::string_view> alpn_protos = {absl::string_view("h2")};
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "\x02h2");

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&client_hello](os_fd_t fd, void* buffer, size_t length,
                                       int flag) -> Api::SysCallSizeResult {
        ENVOY_LOG_MISC(trace, "In mock syscall recv {} {} {} {}", fd, buffer, length, flag);
        ASSERT(length >= client_hello.size());
        memcpy(buffer, client_hello.data(), client_hello.size());
        return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
      }));

  // No event is created if the inline recv parse the hello.
  EXPECT_CALL(dispatcher_,
              createFileEvent_(_, _, Event::FileTriggerType::Edge,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .Times(0);

  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onAccept(cb_));
}

// Test that the deprecated extension name still functions.
TEST(TlsInspectorConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.tls_inspector";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
