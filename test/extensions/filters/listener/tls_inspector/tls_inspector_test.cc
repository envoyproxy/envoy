#include "source/common/common/hex.h"
#include "source/common/http/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/listener/tls_inspector/tls_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "openssl/md5.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
#ifdef WIN32
using testing::Return;
#endif
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
            *store_.rootScope(),
            envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector())),
        io_handle_(Network::SocketInterfaceImpl::makePlatformSpecificSocket(42, false,
                                                                            absl::nullopt, {})) {}

  void init() {
    filter_ = std::make_unique<Filter>(cfg_);

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(
            DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
    buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
        *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
        cfg_->initialReadBufferSize() == 0, cfg_->initialReadBufferSize());
    filter_->onAccept(cb_);
  }

  void mockSysCallForPeek(std::vector<uint8_t>& client_hello, bool windows_recv = false) {
#ifdef WIN32
    // In some cases the syscall used for windows is recv, not readv.
    if (windows_recv) {
      EXPECT_CALL(os_sys_calls_, recv(_, _, _, _))
          .WillOnce(Invoke([=, &client_hello](os_fd_t, void* buffer, size_t length,
                                              int) -> Api::SysCallSizeResult {
            const size_t amount_to_copy = std::min(length, client_hello.size());
            memcpy(buffer, client_hello.data(), amount_to_copy);
            return Api::SysCallSizeResult{ssize_t(amount_to_copy), 0};
          }));
    } else {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(Invoke(
              [&client_hello](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
                const size_t amount_to_copy = std::min(iov->iov_len, client_hello.size());
                memcpy(iov->iov_base, client_hello.data(), amount_to_copy);
                return Api::SysCallSizeResult{ssize_t(amount_to_copy), 0};
              }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    }
#else
    (void)windows_recv;
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(Invoke(
            [&client_hello](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
              const size_t amount_to_copy = std::min(length, client_hello.size());
              memcpy(buffer, client_hello.data(), amount_to_copy);
              return Api::SysCallSizeResult{ssize_t(amount_to_copy), 0};
            }));
#endif
  }

  void testJA3(const std::string& fingerprint, bool expect_server_name = true,
               const std::string& hash = {}, bool expect_alpn = true);
  void testJA4(const std::string& expected_ja4, const std::string& sni = "",
               const std::string& alpn = "");

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Stats::TestUtil::TestStore store_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  Network::MockConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
  Network::IoHandlePtr io_handle_;
  std::unique_ptr<Network::ListenerFilterBufferImpl> buffer_;
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
  proto_config.mutable_max_client_hello_size()->set_value(Config::TLS_MAX_CLIENT_HELLO + 1);
  EXPECT_THROW_WITH_MESSAGE(Config(*store_.rootScope(), proto_config), EnvoyException,
                            "max_client_hello_size of 16385 is greater than maximum of 16384.");
}

// Test that a ClientHello with an SNI value causes the correct name notification.
TEST_P(TlsInspectorTest, SniRegistered) {
  init();
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "");
  mockSysCallForPeek(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
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
  mockSysCallForPeek(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);

  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(client_hello.size(), bytes_processed[0]);
}

// Test with the ClientHello spread over multiple socket reads.
TEST_P(TlsInspectorTest, MultipleReads) {
  init();
  const auto alpn_protos = std::vector<absl::string_view>{Http::Utility::AlpnNames::get().Http2};
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), servername, "\x02h2");
#ifdef WIN32
  {
    InSequence s;
    EXPECT_CALL(os_sys_calls_, readv(_, _, _))
        .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    for (size_t i = 0; i < client_hello.size(); i++) {
      EXPECT_CALL(os_sys_calls_, readv(_, _, _))
          .WillOnce(Invoke([&client_hello, i](os_fd_t fd, const iovec* iov,
                                              int iovcnt) -> Api::SysCallSizeResult {
            ASSERT(iov->iov_len >= client_hello.size());
            memcpy(iov->iov_base, client_hello.data() + i, 1);
            return Api::SysCallSizeResult{ssize_t(1), 0};
          }))
          .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
    }
  }
#else
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
#endif
  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(alpn_protos));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  while (!got_continue) {
    // trigger the event to copy the client hello message into buffer
    EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
    auto state = filter_->onData(*buffer_);
    if (state == Network::FilterStatus::Continue) {
      got_continue = true;
    }
  }
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(client_hello.size(), bytes_processed[0]);
}

// Test that the filter correctly handles a ClientHello with no extensions present.
TEST_P(TlsInspectorTest, NoExtensions) {
  init();
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), "", "");
  mockSysCallForPeek(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_found_.value());
  EXPECT_EQ(1, cfg_->stats().sni_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().alpn_not_found_.value());
}

// Test that the filter fails if the ClientHello is larger than the
// maximum allowed size.
TEST_P(TlsInspectorTest, ClientHelloTooBig) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_close_connection_on_client_hello_parsing_errors(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "769,47-53-5-10-49161-49162-49171-49172-50-56-19-4,0-10-11,23-24-25,0", 17000);
  ASSERT(client_hello.size() > Config::TLS_MAX_CLIENT_HELLO);

  filter_ = std::make_unique<Filter>(cfg_);
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(0);
  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
  EXPECT_CALL(dispatcher_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
      *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
      cfg_->maxClientHelloSize() == 0, cfg_->maxClientHelloSize());

  filter_->onAccept(cb_);
  mockSysCallForPeek(client_hello, true);
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  Protobuf::Struct expected_metadata;
  auto& fields = *expected_metadata.mutable_fields();
  fields[Filter::failureReasonKey()].set_string_value(Filter::failureReasonClientHelloTooLarge());
  EXPECT_CALL(cb_, setDynamicMetadata(Filter::dynamicMetadataKey(), ProtoEq(expected_metadata)));

  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);
  EXPECT_EQ(1, cfg_->stats().client_hello_too_large_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ("TLS_error|error:10000092:SSL "
            "routines:OPENSSL_internal:ENCRYPTED_LENGTH_TOO_LONG:TLS_error_end",
            cb_.streamInfo().downstreamTransportFailureReason());
}

TEST_P(TlsInspectorTest, ClientHelloTooBigTreatParsingErrorAsPlainText) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(
      "769,47-53-5-10-49161-49162-49171-49172-50-56-19-4,0-10-11,23-24-25,0", 17000);
  ASSERT(client_hello.size() > Config::TLS_MAX_CLIENT_HELLO);

  filter_ = std::make_unique<Filter>(cfg_);
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(0);
  EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
  EXPECT_CALL(dispatcher_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(
          DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
      *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
      cfg_->maxClientHelloSize() == 0, cfg_->maxClientHelloSize());

  filter_->onAccept(cb_);
  mockSysCallForPeek(client_hello, true);
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(5, bytes_processed[0]);
}

// Test that the filter sets the `JA3` hash
TEST_P(TlsInspectorTest, ConnectionFingerprint) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja3_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), "", "");
  init();
  mockSysCallForPeek(client_hello);
  EXPECT_CALL(socket_, setJA3Hash(_));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

void TlsInspectorTest::testJA3(const std::string& fingerprint, bool expect_server_name,
                               const std::string& hash, bool expect_alpn) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja3_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloFromJA3Fingerprint(fingerprint);
  init();
  mockSysCallForPeek(client_hello);
  if (hash.empty()) {
    uint8_t buf[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const uint8_t*>(fingerprint.data()), fingerprint.size(), buf);
    EXPECT_CALL(socket_, setJA3Hash(absl::string_view(Envoy::Hex::encode(buf, MD5_DIGEST_LENGTH))));
  } else {
    EXPECT_CALL(socket_, setJA3Hash(absl::string_view(hash)));
  }
  if (expect_server_name) {
    EXPECT_CALL(socket_, setRequestedServerName(absl::string_view("www.envoyproxy.io")));
  }
  if (expect_alpn) {
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(testing::Contains("HTTP/1.1")));
  } else {
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  }
  // EXPECT_CALL(cb_, continueFilterChain(true));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

// Test that the filter sets the correct `JA3` hash.
// Fingerprint created with User-Agent "curl/7.64.1" and a request to ja3er.com/json.
TEST_P(TlsInspectorTest, ConnectionJA3Hash) {
  testJA3("771,49200-49196-49192-49188-49172-49162-159-107-57-52393-52392-52394-65413-196-136-"
          "129-157-61-53-192-132-49199-49195-49191-49187-49171-49161-158-103-51-190-69-156-60-"
          "47-186-65-49169-49159-5-4-49170-49160-22-10-255,0-11-10-13-16,29-23-24,0");
}

// Test that the filter sets the correct `JA3` hash with GREASE values in ClientHello message.
// Fingerprint created with User-Agent "curl/7.64.1" and a request to ja3er.com/json.
TEST_P(TlsInspectorTest, ConnectionJA3HashGREASE) {
  const std::string version("771");
  const std::string ciphers(
      "49200-49196-49192-49188-49172-49162-159-107-57-52393-52392-52394-65413-196-136-"
      "129-157-61-53-192-132-49199-49195-49191-49187-49171-49161-158-103-51-190-69-156-60-"
      "47-186-65-49169-49159-5-4-49170-49160-22-10-255");
  const std::string extensions_ec_formats("0-11-10-13-16,29-23-24,0");
  std::string fingerprint;
  absl::StrAppend(&fingerprint, version, ",", ciphers, ",", extensions_ec_formats);

  std::string grease;
  for (uint32_t i = 0x0a0a; i < 0xfafa; i += 0x1010) {
    if (i != 0x0a0a) {
      absl::StrAppend(&grease, "-");
    }
    absl::StrAppendFormat(&grease, "%d", i);
  }
  std::string fingerprint_with_grease;
  absl::StrAppend(&fingerprint_with_grease, version, ",", grease, "-", ciphers, ",", grease, "-",
                  extensions_ec_formats);

  uint8_t buf[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<const uint8_t*>(fingerprint.data()), fingerprint.size(), buf);
  std::string hash = Envoy::Hex::encode(buf, MD5_DIGEST_LENGTH);

  testJA3(fingerprint_with_grease, true, hash);
}

// Test that the filter sets the correct `JA3` hash with no elliptic curves or elliptic curve point
// formats in ClientHello message. Fingerprint is from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashNoEllipticCurvesOrPointFormats) {
  testJA3("771,157-49313-49309-156-49312-49308-61-60-53-47-255,0-35-16-22-23-13,,");
}

// Test that the filter sets the correct `JA3` hash with TLS1.0 and no extensions in ClientHello
// message. Fingerprint is from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashTls10NoExtensions) {
  testJA3("769,49162-49157-49161-49156-49159-49154-49160-49155-49172-49167-49171-49166-49169-49164-"
          "49170-49165-57-51-53-47-5-4-10,,,",
          false, "", false);
}

// Test that the filter sets the correct `JA3` hash with TLS1.1.
// Fingerprint is from ja3er.com/getAllHashesJson.
TEST_P(TlsInspectorTest, ConnectionJA3HashTls11) {
  testJA3("770,49162-49172-49161-49171-57-56-51-50-53-47-255,0-11-10-16-22-23,5,0-1-2");
}

// Test that the filter fails on non-SSL data
TEST_P(TlsInspectorTest, NotSsl) {
  init();
  std::vector<uint8_t> data;

  // Use 100 bytes of zeroes. This is not valid as a ClientHello.
  data.resize(100);
  mockSysCallForPeek(data);
  // trigger the event to copy the client hello message into buffer:q
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  Protobuf::Struct expected_metadata;
  auto& fields = *expected_metadata.mutable_fields();
  fields[Filter::failureReasonKey()].set_string_value(
      Filter::failureReasonClientHelloNotDetected());
  EXPECT_CALL(cb_, setDynamicMetadata(Filter::dynamicMetadataKey(), ProtoEq(expected_metadata)));

  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(5, bytes_processed[0]);
  EXPECT_EQ(
      "TLS_error|error:100000f7:SSL routines:OPENSSL_internal:WRONG_VERSION_NUMBER:TLS_error_end",
      cb_.streamInfo().downstreamTransportFailureReason());
}

TEST_P(TlsInspectorTest, NotSslCloseConnection) {
  std::vector<uint8_t> data;

  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.set_close_connection_on_client_hello_parsing_errors(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  init();

  // Use 100 bytes of zeroes. This is not valid as a ClientHello.
  data.resize(100);
  mockSysCallForPeek(data);
  // trigger the event to copy the client hello message into buffer:q
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  Protobuf::Struct expected_metadata;
  auto& fields = *expected_metadata.mutable_fields();
  fields[Filter::failureReasonKey()].set_string_value(
      Filter::failureReasonClientHelloNotDetected());
  EXPECT_CALL(cb_, setDynamicMetadata(Filter::dynamicMetadataKey(), ProtoEq(expected_metadata)));

  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(5, bytes_processed[0]);
  EXPECT_EQ(
      "TLS_error|error:100000f7:SSL routines:OPENSSL_internal:WRONG_VERSION_NUMBER:TLS_error_end",
      cb_.streamInfo().downstreamTransportFailureReason());
}

// Verify that a plain text connection with a single I/O read of more than
// maximum TLS inspector buffer (currently 16Kb) is correctly detected.
TEST_P(TlsInspectorTest, NotSslOverMaxReadBytesSingleRead) {
  init();
  std::vector<uint8_t> data;

  // Use more than max number of bytes for a ClientHello.
  data.resize(Config::TLS_MAX_CLIENT_HELLO + 1);
  mockSysCallForPeek(data);
  // trigger the event to copy the client hello message into buffer:q
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
  const std::vector<uint64_t> bytes_processed =
      store_.histogramValues("tls_inspector.bytes_processed", false);
  ASSERT_EQ(1, bytes_processed.size());
  EXPECT_EQ(5, bytes_processed[0]);
}

TEST_P(TlsInspectorTest, EarlyTerminationShouldNotRecordBytesProcessed) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), "example.com", "");

  // Clobber half of client_hello so we don't have sufficient bytes to finish inspection.
  client_hello.resize(client_hello.size() / 2);

  init();
  mockSysCallForPeek(client_hello);
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  // Trigger the event to copy the client hello message into buffer
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);

  // Terminate early.
  filter_.reset();

  ASSERT_FALSE(store_.histogramRecordedValues("tls_inspector.bytes_processed"));
}

TEST_P(TlsInspectorTest, RequestedMaxReadSizeDoesNotGoBeyondMaxSize) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  const uint32_t initial_buffer_size = 15;
  const size_t max_size = 50;
  proto_config.mutable_initial_read_buffer_size()->set_value(initial_buffer_size);
  proto_config.mutable_max_client_hello_size()->set_value(max_size);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);
  buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
      *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
      cfg_->initialReadBufferSize() == 0, cfg_->initialReadBufferSize());
  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), "example.com", "\x02h2");

  init();
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(_)).Times(0);

  mockSysCallForPeek(client_hello, true);
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);
  EXPECT_EQ(2 * initial_buffer_size, filter_->maxReadBytes());
  buffer_->resetCapacity(2 * initial_buffer_size);

  mockSysCallForPeek(client_hello, true);
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);
  EXPECT_EQ(max_size, filter_->maxReadBytes());
  buffer_->resetCapacity(max_size);

  // The filter should not request a larger buffer as we've reached the max.
  // It should close the connection.
  mockSysCallForPeek(client_hello, true);
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, state);
  EXPECT_EQ(max_size, filter_->maxReadBytes());
  EXPECT_FALSE(io_handle_->isOpen());
}

void TlsInspectorTest::testJA4(const std::string& expected_ja4, const std::string& sni,
                               const std::string& alpn) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), sni, alpn);

  init();
  mockSysCallForPeek(client_hello);

  EXPECT_CALL(socket_, setJA4Hash(absl::string_view(expected_ja4)));

  if (!sni.empty()) {
    EXPECT_CALL(socket_, setRequestedServerName(absl::string_view(sni)));
  }

  if (alpn.empty() || alpn[0] == 0) {
    // No ALPN extension at all or empty ALPN list
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);
  } else {
    // Normal ALPN list
    std::vector<absl::string_view> alpn_protos;
    size_t start = 0;
    while (start < alpn.size()) {
      size_t len = static_cast<size_t>(alpn[start]);
      alpn_protos.push_back(absl::string_view(alpn.data() + start + 1, len));
      start += len + 1;
    }
    EXPECT_CALL(socket_, setRequestedApplicationProtocols(testing::ContainerEq(alpn_protos)));
  }

  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

const absl::flat_hash_map<uint16_t, std::string> basic_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i040500_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11i040500_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12i100600_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13i030500_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4Basic) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  std::string expected_value = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                                max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                   ? "t13i130900_f57a46bbacb6_78e6aca7449b"
                                   : basic_test_version_to_ja4_.at(min_version);

  testJA4(expected_value);
}

const absl::flat_hash_map<uint16_t, std::string> sni_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10d040600_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11d040600_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12d100700_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13d030600_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4WithSNI) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  std::string expected_value = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                                max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                   ? "t13d131000_f57a46bbacb6_78e6aca7449b"
                                   : sni_test_version_to_ja4_.at(min_version);

  testJA4(expected_value, "example.com");
}

const absl::flat_hash_map<uint16_t, std::string> alpn_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i0406h2_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11i0406h2_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12i1007h2_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13i0306h2_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4WithALPN) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  std::string expected_value = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                                max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                   ? "t13i1310h2_f57a46bbacb6_78e6aca7449b"
                                   : alpn_test_version_to_ja4_.at(min_version);

  testJA4(expected_value, "", "\x02h2\x08http/1.1");
}

const absl::flat_hash_map<uint16_t, std::string> alpn_sni_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10d0407h2_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11d0407h2_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12d1008h2_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13d0307h2_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4WithSNIAndALPN) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  std::string expected_value = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                                max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                   ? "t13d1312h2_f57a46bbacb6_ef7df7f74e48"
                                   : alpn_sni_test_version_to_ja4_.at(min_version);

  testJA4(expected_value, "example.com", "\x02h2\x08http/1.1");
}

const absl::flat_hash_map<uint16_t, std::string> alpn_single_char_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i0406hh_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11i0406hh_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12i1007hh_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13i0306hh_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4WithSingleCharacterALPN) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  // Create single character ALPN
  std::string alpn;
  alpn.push_back(0x01); // length
  alpn.push_back('h');  // single character

  std::string expected_ja4 = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                              max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                 ? "t13i1310hh_f57a46bbacb6_78e6aca7449b" // same char repeated
                                 : alpn_single_char_test_version_to_ja4_.at(min_version);

  testJA4(expected_ja4, "", alpn);
}

const absl::flat_hash_map<uint16_t, std::string> no_alpn_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i040500_cefcabfea53d_950472255fe9"},
    {TLS1_1_VERSION, "t11i040500_cefcabfea53d_950472255fe9"},
    {TLS1_2_VERSION, "t12i100600_a8cf61a50a39_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13i030500_55b375c5d22e_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4WithEmptyALPN) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  // Create empty ALPN list
  std::string alpn;
  alpn.push_back(0x00); // zero length

  std::string expected_ja4 = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                              max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                 ? "t13i130900_f57a46bbacb6_78e6aca7449b" // "00" for empty ALPN
                                 : no_alpn_test_version_to_ja4_.at(min_version);

  testJA4(expected_ja4, "", alpn);
}

TEST_P(TlsInspectorTest, JA4MalformedClientHello) {
  init();
  std::vector<uint8_t> malformed_hello(50, 0); // Invalid ClientHello
  mockSysCallForPeek(malformed_hello);

  EXPECT_CALL(socket_, setJA4Hash(_)).Times(0);
  EXPECT_CALL(socket_, setDetectedTransportProtocol(_)).Times(0);

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
  EXPECT_EQ(1, cfg_->stats().tls_not_found_.value());
}

const absl::flat_hash_map<uint16_t, std::string> no_ext_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i020000_04659ec43a24_000000000000"},
    {TLS1_1_VERSION, "t11i020000_04659ec43a24_000000000000"},
    {TLS1_2_VERSION, "t12i020000_04659ec43a24_000000000000"},
    {TLS1_3_VERSION, "t13i020000_62ed6f6ca7ad_000000000000"}};

TEST_P(TlsInspectorTest, JA4WithNoExtensions) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  // Generate ClientHello with no extensions
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHelloWithoutExtensions(std::get<1>(GetParam()));

  init();
  mockSysCallForPeek(client_hello);

  std::string expected_ja4 = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                              max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                 ? "t13i020000_62ed6f6ca7ad_000000000000" // "00" for empty ALPN
                                 : no_ext_test_version_to_ja4_.at(min_version);

  EXPECT_CALL(socket_, setJA4Hash(absl::string_view(expected_ja4)));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

TEST_P(TlsInspectorTest, JA4VersionFallback) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  // Create ClientHello without supported_versions extension
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(TLS1_2_VERSION, TLS1_2_VERSION, "", "");

  init();
  mockSysCallForPeek(client_hello);

  // Should fall back to ClientHello version field
  std::string expected_ja4 = "t12i100600_a8cf61a50a39_0f3b2bcde21d";
  EXPECT_CALL(socket_, setJA4Hash(absl::string_view(expected_ja4)));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);           // No SNI in this test
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0); // No ALPN in this test

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

const absl::flat_hash_map<uint16_t, std::string> empty_ext_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i010000_0f2cb44170f4_000000000000"},
    {TLS1_1_VERSION, "t11i010000_0f2cb44170f4_000000000000"},
    {TLS1_2_VERSION, "t12i010000_0f2cb44170f4_000000000000"},
    {TLS1_3_VERSION, "t13i010000_0f2cb44170f4_000000000000"}};

TEST_P(TlsInspectorTest, JA4EmptyExtensionsList) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  std::vector<uint8_t> client_hello = Tls::Test::generateClientHelloEmptyExtensions(max_version);

  init();
  mockSysCallForPeek(client_hello);

  // Set up proper expectations for socket calls
  std::string expected_ja4 = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                              max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                 ? "t13i010000_0f2cb44170f4_000000000000"
                                 : empty_ext_test_version_to_ja4_.at(min_version);
  EXPECT_CALL(socket_, setJA4Hash(absl::string_view(expected_ja4)));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

const absl::flat_hash_map<uint16_t, std::string> max_ciphers_test_version_to_ja4_ = {
    {TLS1_VERSION, "t10i990500_f254cf4fa23b_950472255fe9"},
    {TLS1_1_VERSION, "t11i990500_f254cf4fa23b_950472255fe9"},
    {TLS1_2_VERSION, "t12i990600_f254cf4fa23b_0f3b2bcde21d"},
    {TLS1_3_VERSION, "t13i990500_b33cacf22aea_678be4e4848e"}};

TEST_P(TlsInspectorTest, JA4MaxValuesCiphers) {
  const uint16_t min_version = std::get<0>(GetParam());
  const uint16_t max_version = std::get<1>(GetParam());

  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  // Start with basic ClientHello
  std::vector<uint8_t> client_hello =
      Tls::Test::generateClientHello(std::get<0>(GetParam()), std::get<1>(GetParam()), "", "");

  // First, find the cipher suites section
  size_t session_id_len_pos = 43; // 5(record) + 4(handshake) + 2(version) + 32(random)
  uint8_t session_id_len = client_hello[session_id_len_pos];
  size_t cipher_suites_len_pos = session_id_len_pos + 1 + session_id_len;

  // Get the original cipher suites length
  uint16_t original_cipher_length =
      (client_hello[cipher_suites_len_pos] << 8) | client_hello[cipher_suites_len_pos + 1];

  // Create new cipher suites
  std::vector<uint8_t> new_ciphers;
  const uint16_t num_ciphers = 100;
  uint16_t cipher_length = num_ciphers * 2;

  // Add length prefix
  new_ciphers.push_back((cipher_length >> 8) & 0xFF);
  new_ciphers.push_back(cipher_length & 0xFF);

  // Add cipher suites
  for (uint16_t i = 0; i < num_ciphers; i++) {
    if (std::get<1>(GetParam()) >= TLS1_3_VERSION) {
      // Use a variety of TLS 1.3 ciphers
      switch (i % 5) {
      case 0:
        new_ciphers.push_back(0x13);
        new_ciphers.push_back(0x01); // TLS_AES_128_GCM_SHA256
        break;
      case 1:
        new_ciphers.push_back(0x13);
        new_ciphers.push_back(0x02); // TLS_AES_256_GCM_SHA384
        break;
      case 2:
        new_ciphers.push_back(0x13);
        new_ciphers.push_back(0x03); // TLS_CHACHA20_POLY1305_SHA256
        break;
      case 3:
        new_ciphers.push_back(0x13);
        new_ciphers.push_back(0x04); // TLS_AES_128_CCM_SHA256
        break;
      case 4:
        new_ciphers.push_back(0x13);
        new_ciphers.push_back(0x05); // TLS_AES_128_CCM_8_SHA256
        break;
      }
    } else {
      // Use a variety of TLS 1.2 ciphers
      switch (i % 5) {
      case 0:
        new_ciphers.push_back(0xc0);
        new_ciphers.push_back(0x2f); // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
        break;
      case 1:
        new_ciphers.push_back(0xc0);
        new_ciphers.push_back(0x30); // TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
        break;
      case 2:
        new_ciphers.push_back(0xc0);
        new_ciphers.push_back(0x2b); // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
        break;
      case 3:
        new_ciphers.push_back(0xc0);
        new_ciphers.push_back(0x2c); // TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
        break;
      case 4:
        new_ciphers.push_back(0xcc);
        new_ciphers.push_back(0xa9); // TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256
        break;
      }
    }
  }

  // Create modified ClientHello
  std::vector<uint8_t> modified_hello;

  // Copy up to cipher suites
  modified_hello.insert(modified_hello.end(), client_hello.begin(),
                        client_hello.begin() + cipher_suites_len_pos);

  // Add new cipher suites
  modified_hello.insert(modified_hello.end(), new_ciphers.begin(), new_ciphers.end());

  // Copy remaining data after original cipher suites
  modified_hello.insert(modified_hello.end(),
                        client_hello.begin() + cipher_suites_len_pos + 2 + original_cipher_length,
                        client_hello.end());

  // Update record layer length
  size_t record_length = modified_hello.size() - 5;
  modified_hello[3] = (record_length >> 8) & 0xFF;
  modified_hello[4] = record_length & 0xFF;

  // Update handshake message length
  size_t handshake_length = record_length - 4;
  modified_hello[6] = (handshake_length >> 16) & 0xFF;
  modified_hello[7] = (handshake_length >> 8) & 0xFF;
  modified_hello[8] = handshake_length & 0xFF;

  init();
  mockSysCallForPeek(modified_hello);

  // Set up proper expectations for socket calls
  std::string expected_ja4 = (min_version == Config::TLS_MIN_SUPPORTED_VERSION &&
                              max_version == Config::TLS_MAX_SUPPORTED_VERSION)
                                 ? "t13i990900_b33cacf22aea_78e6aca7449b"
                                 : max_ciphers_test_version_to_ja4_.at(min_version);

  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());
  EXPECT_CALL(socket_, setJA4Hash(expected_ja4));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(_)).Times(0);

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

TEST_P(TlsInspectorTest, JA4NonAlphanumericALPN) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  cfg_ = std::make_shared<Config>(*store_.rootScope(), proto_config);

  // Create ALPN with non-alphanumeric characters at start and end
  std::string special_alpn;
  special_alpn.push_back(0x03); // Length 3
  special_alpn.push_back('*');  // Special char at start
  special_alpn.push_back('2');  // Middle char
  special_alpn.push_back(')');  // Special char at end

  std::vector<uint8_t> client_hello = Tls::Test::generateClientHello(
      std::get<0>(GetParam()), std::get<1>(GetParam()), "", special_alpn);

  init();
  mockSysCallForPeek(client_hello);

  // This should trigger the code path that converts non-alphanumeric chars to hex
  EXPECT_CALL(socket_, setJA4Hash(testing::_));
  EXPECT_CALL(socket_, setDetectedTransportProtocol(absl::string_view("tls")));
  EXPECT_CALL(socket_, detectedTransportProtocol()).Times(::testing::AnyNumber());

  // No server name in this test, so shouldn't call this method
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);

  std::vector<absl::string_view> expected_alpn = {"*2)"};
  EXPECT_CALL(socket_, setRequestedApplicationProtocols(testing::ContainerEq(expected_alpn)));

  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  auto state = filter_->onData(*buffer_);
  EXPECT_EQ(Network::FilterStatus::Continue, state);
}

} // namespace
} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
