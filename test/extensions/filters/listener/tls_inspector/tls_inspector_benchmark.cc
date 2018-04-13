#include <vector>

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "testing/base/public/benchmark.h"

using testing::AtLeast;
using testing::Invoke;
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

class FastMockListenerFilterCallbacks : public Network::MockListenerFilterCallbacks {
public:
  FastMockListenerFilterCallbacks(Network::ConnectionSocket& socket, Event::Dispatcher& dispatcher)
      : socket_(socket), dispatcher_(dispatcher) {}
  Network::ConnectionSocket& socket() override { return socket_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void continueFilterChain(bool success) override { RELEASE_ASSERT(success); }

  Network::ConnectionSocket& socket_;
  Event::Dispatcher& dispatcher_;
};

class FastMockConnectionSocket : public Network::MockConnectionSocket {
public:
  int fd() const override { return 42; }
  void setRequestedServerName(absl::string_view server_name) override {
    if (server_name == "example.com") {
      got_server_name_ = true;
    }
  }

  bool got_server_name_{false};
};

// Don't inherit from the mock implementation at all, because this is instantiated
// in the hot loop.
class FastMockFileEvent : public Event::FileEvent {
  void activate(uint32_t) override {}
  void setEnabled(uint32_t) override {}
};

class FastMockDispatcher : public Event::MockDispatcher {
public:
  Event::FileEventPtr createFileEvent(int, Event::FileReadyCb cb, Event::FileTriggerType,
                                      uint32_t) override {
    file_event_callback_ = cb;
    return std::make_unique<FastMockFileEvent>();
  }

  Event::FileReadyCb file_event_callback_;
};

class FastMockOsSysCalls : public Api::MockOsSysCalls {
public:
  FastMockOsSysCalls(const std::vector<uint8_t>& client_hello) : client_hello_(client_hello) {}

  ssize_t recv(int, void* buffer, size_t length, int) override {
    RELEASE_ASSERT(length >= client_hello_.size());
    memcpy(buffer, client_hello_.data(), client_hello_.size());
    return client_hello_.size();
  }

  const std::vector<uint8_t> client_hello_;
};

static std::vector<uint8_t> generateClientHello(const std::string& sni_name) {
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
  std::vector<uint8_t> buf(data, data + data_len);
  return buf;
}

static void BM_TlsInspector(benchmark::State& state) {
  NiceMock<FastMockOsSysCalls> os_sys_calls(generateClientHello("example.com"));
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls};
  NiceMock<Stats::MockStore> store;
  ConfigSharedPtr cfg(std::make_shared<Config>(store));
  FastMockConnectionSocket socket;
  NiceMock<FastMockDispatcher> dispatcher;
  FastMockListenerFilterCallbacks cb(socket, dispatcher);

  for (auto _ : state) {
    Filter filter(cfg);
    filter.onAccept(cb);
    dispatcher.file_event_callback_(Event::FileReadyType::Read);
    RELEASE_ASSERT(socket.got_server_name_);
    socket.got_server_name_ = false;
  }
}

BENCHMARK(BM_TlsInspector)->Unit(benchmark::kMicrosecond);

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Registry::initialize(spdlog::level::warn,
                                      Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
