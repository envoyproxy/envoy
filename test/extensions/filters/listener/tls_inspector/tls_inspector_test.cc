#include "source/common/http/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
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
      : cfg_(std::make_shared<Config>(
            store_, envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector())),
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
              return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
            }));
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                              Event::FileReadyType::Read))
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
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  EXPECT_THROW_WITH_MESSAGE(Config(store_, proto_config, Config::TLS_MAX_CLIENT_HELLO + 1),
                            EnvoyException,
                            "max_client_hello_size of 65537 is greater than maximum of 65536.");
}

// Test that the filter detects Closed events and terminates.
TEST_P(TlsInspectorTest, ConnectionClosed) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Return(Api::SysCallSizeResult{0, 0}));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_EQ(1, cfg_->stats().connection_closed_.value());
}

// Test that the filter detects detects read errors.
TEST_P(TlsInspectorTest, ReadError) {
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(InvokeWithoutArgs([]() {
    return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_NOT_SUP};
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
  const auto alpn_protos = std::vector<absl::string_view>{Http::Utility::AlpnNames::get().Http2,
                                                          Http::Utility::AlpnNames::get().Http11};
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
  const auto alpn_protos = std::vector<absl::string_view>{Http::Utility::AlpnNames::get().Http2};
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "\x02h2");
  {
    InSequence s;
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(InvokeWithoutArgs([]() -> Api::SysCallSizeResult {
          return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
        }));
    for (size_t i = 1; i <= client_hello.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke([&client_hello, i](os_fd_t, void* buffer, size_t length,
                                              int) -> Api::SysCallSizeResult {
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
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  const size_t max_size = 50;
  cfg_ = std::make_shared<Config>(store_, proto_config, static_cast<uint32_t>(max_size));
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

// Test that the filter sets the ja3 hash
TEST_P(TlsInspectorTest, ConnectionFingerprint) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), "", "");
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(_));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter sets the correct ja3 hash.
// Fingerprint created with User-Agent "curl/7.64.1" and a request to ja3er.com/json.
TEST_P(TlsInspectorTest, ConnectionJA3Hash) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "771,49200-49196-49192-49188-49172-49162-159-107-57-52393-52392-52394-65413-196-136-"
      "129-157-61-53-192-132-49199-49195-49191-49187-49171-49161-158-103-51-190-69-156-60-"
      "47-186-65-49169-49159-5-4-49170-49160-22-10-255,0-11-10-13-16,29-23-24,0");
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(absl::string_view("3faa4ad39f690c4ef1c3160caa375465")));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter sets the correct ja3 hash with GREASE values in ClientHello message.
// Fingerprint created with User-Agent "curl/7.64.1" and a request to ja3er.com/json.
TEST_P(TlsInspectorTest, ConnectionJA3HashGREASE) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::string grease;
  for (uint32_t i = 0x0a0a; i < 0xfafa; i += 0x1010) {
    if (i != 0x0a0a) {
      absl::StrAppend(&grease, "-");
    }
    absl::StrAppendFormat(&grease, "%d", i);
  }
  std::string fingerprint("771,");
  absl::StrAppend(&fingerprint, grease);
  absl::StrAppend(
      &fingerprint,
      "-49200-49196-49192-49188-49172-49162-159-107-57-52393-52392-52394-65413-196-136-"
      "129-157-61-53-192-132-49199-49195-49191-49187-49171-49161-158-103-51-190-69-156-60-"
      "47-186-65-49169-49159-5-4-49170-49160-22-10-255,");
  absl::StrAppend(&fingerprint, grease);
  absl::StrAppend(&fingerprint, "-0-11-10-13-16,29-23-24,0");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(fingerprint);
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(absl::string_view("3faa4ad39f690c4ef1c3160caa375465")));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter sets the correct ja3 hash with no elliptic curves or elliptic curve point
// formats in ClientHello message. Fingerprint and hash are from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashNoEllipticCurvesOrPointFormats) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "771,4865-4866-4867-4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-"
      "47-53-10,"
      "35-16-5-13-18-51-45-43-27-0-23-65281-10-11,,");
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(absl::string_view("00359581639dba249d0c7384a70056f9")));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter sets the correct ja3 hash with TLS1.0 and no extensions in ClientHello
// message. Fingerprint and hash are from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashTls10NoExtensions) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "769,49162-49157-49161-49156-49159-49154-49160-49155-49172-49167-49171-49166-49169-49164-"
      "49170-49165-57-51-53-47-5-4-10,,,");
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(absl::string_view("06207a1730b5deeb207b0556e102ded2")));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter sets the correct ja3 hash with TLS1.1.
// Fingerprint and hash are from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashTls11) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_enable_ja3_fingerprinting(true);
  cfg_ = std::make_shared<Config>(store_, proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "770,49162-49172-49161-49171-57-51-53-47-255,0-11-10-13172-16-22-23,18,0-1-2");
  init();
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke(
          [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return Api::SysCallSizeResult{ssize_t(client_hello.size()), 0};
          }));
  EXPECT_CALL(socket_, setJA3Hash(absl::string_view("009793ecc75f2d1d47d3051ff8fb0309")));
  EXPECT_CALL(socket_, setRequestedServerName(_));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  file_event_callback_(Event::FileReadyType::Read);
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
  const auto alpn_protos = std::vector<absl::string_view>{Http::Utility::AlpnNames::get().Http2};
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
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .Times(0);

  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onAccept(cb_));
}

// Test that the deprecated extension name is disabled by default.
// TODO(zuercher): remove when envoy.deprecated_features.allow_deprecated_extension_names is removed
TEST(TlsInspectorConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.tls_inspector";

  ASSERT_EQ(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
