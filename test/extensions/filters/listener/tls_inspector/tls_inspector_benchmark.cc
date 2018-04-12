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
  state.PauseTiming();
  const std::vector<uint8_t> client_hello(generateClientHello("example.com"));
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls};
  NiceMock<Stats::MockStore> store;
  ConfigSharedPtr cfg(std::make_shared<Config>(store));
  Network::MockListenerFilterCallbacks cb;
  Network::MockConnectionSocket socket;
  NiceMock<Event::MockDispatcher> dispatcher;
  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(cb, socket()).WillRepeatedly(ReturnRef(socket));
  EXPECT_CALL(cb, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(cb, continueFilterChain(true)).Times(AtLeast(1));
  EXPECT_CALL(socket, fd()).WillRepeatedly(Return(42));
  EXPECT_CALL(socket, setRequestedServerName(_)).Times(AtLeast(1));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _))
      .WillRepeatedly(
          DoAll(SaveArg<1>(&file_event_callback), ReturnNew<NiceMock<Event::MockFileEvent>>()));
  EXPECT_CALL(os_sys_calls, recv(42, _, _, MSG_PEEK))
      .WillRepeatedly(Invoke([&client_hello](int, void* buffer, size_t length, int) -> int {
        ASSERT(length >= client_hello.size());
        memcpy(buffer, client_hello.data(), client_hello.size());
        return client_hello.size();
      }));

  state.ResumeTiming();
  for (auto _ : state) {
    Filter filter(cfg);
    filter.onAccept(cb);
    file_event_callback(Event::FileReadyType::Read);
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
