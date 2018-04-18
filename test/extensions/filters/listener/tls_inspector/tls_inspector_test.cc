#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

// Build again on the basis of the connection_handler_test.cc

class TlsInspectorTest : public testing::Test {
public:
  TlsInspectorTest() : cfg_(std::make_shared<Config>(store_)) {}

  void init() { init(std::make_unique<Filter>(cfg_)); }

  void init(std::unique_ptr<Filter>&& filter) {
    filter_ = std::move(filter);
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

  std::vector<uint8_t> generateClientHello(const std::string& sni_name) {
    const SSL_METHOD* method = SSLv23_method();
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(method));

    const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
    SSL_CTX_set_options(ctx.get(), flags);

    bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));

    // Ownership of these is passed to *ssl
    BIO* in = BIO_new(BIO_s_mem());
    BIO* out = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl.get(), in, out);

    SSL_set_connect_state(ssl.get());
    const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
    SSL_set_cipher_list(ssl.get(), PREFERRED_CIPHERS);
    if (!sni_name.empty()) {
      SSL_set_tlsext_host_name(ssl.get(), sni_name.c_str());
    }
    SSL_do_handshake(ssl.get());
    const uint8_t* data = NULL;
    size_t data_len = 0;
    BIO_mem_contents(out, &data, &data_len);
    ASSERT(data_len > 0);
    std::vector<uint8_t> buf(data, data + data_len);
    return buf;
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  NiceMock<Stats::MockStore> store_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  Network::MockListenerFilterCallbacks cb_;
  Network::MockConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
};

// Test that the filter detects Closed events and terminates.
TEST_F(TlsInspectorTest, ConnectionClosed) {
  init();
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Closed);
}

// Test that a ClientHello with an SNI value causes the correct name notification.
TEST_F(TlsInspectorTest, SniRegistered) {
  init();
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = generateClientHello(servername);
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&client_hello](int, void* buffer, size_t length, int) -> int {
        ASSERT(length >= client_hello.size());
        memcpy(buffer, client_hello.data(), client_hello.size());
        return client_hello.size();
      }));
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test with the ClientHello spread over multiple socket reads.
TEST_F(TlsInspectorTest, MultipleReads) {
  init();
  const std::string servername("example.com");
  std::vector<uint8_t> client_hello = generateClientHello(servername);
  {
    InSequence s;
    for (size_t i = 1; i <= client_hello.size(); i++) {
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(Invoke([&client_hello, i](int, void* buffer, size_t length, int) -> int {
            ASSERT(length >= client_hello.size());
            memcpy(buffer, client_hello.data(), client_hello.size());
            return i;
          }));
    }
  }

  bool got_continue = false;
  EXPECT_CALL(socket_, setRequestedServerName(Eq(servername)));
  EXPECT_CALL(cb_, continueFilterChain(true)).WillOnce(InvokeWithoutArgs([&got_continue]() {
    got_continue = true;
  }));
  while (!got_continue) {
    file_event_callback_(Event::FileReadyType::Read);
  }
}

// Test that the filter correctly handles a ClientHello with no SNI present
TEST_F(TlsInspectorTest, NoSni) {
  init();
  std::vector<uint8_t> client_hello = generateClientHello("");
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&client_hello](int, void* buffer, size_t length, int) -> int {
        ASSERT(length >= client_hello.size());
        memcpy(buffer, client_hello.data(), client_hello.size());
        return client_hello.size();
      }));
  EXPECT_CALL(socket_, setRequestedServerName(_)).Times(0);
  EXPECT_CALL(cb_, continueFilterChain(true));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter fails if the ClientHello is larger than the
// maximum allowed size.
TEST_F(TlsInspectorTest, ClientHelloTooBig) {
  std::vector<uint8_t> client_hello = generateClientHello("example.com");
  const size_t max_size = 50;
  ASSERT(client_hello.size() > max_size);
  init(std::make_unique<Filter>(cfg_, max_size));
  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&client_hello](int, void* buffer, size_t length, int) -> int {
        ASSERT(length == max_size);
        memcpy(buffer, client_hello.data(), length);
        return length;
      }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
}

// Test that the filter fails on non-SSL data
TEST_F(TlsInspectorTest, NotSsl) {
  init();
  std::vector<uint8_t> data;

  // Use 100 bytes of zeroes. This is not valid as a ClientHello.
  data.resize(100);

  EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
      .WillOnce(Invoke([&data](int, void* buffer, size_t length, int) -> int {
        ASSERT(length >= data.size());
        memcpy(buffer, data.data(), data.size());
        return data.size();
      }));
  EXPECT_CALL(cb_, continueFilterChain(false));
  file_event_callback_(Event::FileReadyType::Read);
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
