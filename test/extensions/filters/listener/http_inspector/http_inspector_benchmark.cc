#include <vector>

#include "common/common/hex.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"

#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

class FastMockListenerFilterCallbacks : public Network::MockListenerFilterCallbacks {
public:
  FastMockListenerFilterCallbacks(Network::ConnectionSocket& socket, Event::Dispatcher& dispatcher)
      : socket_(socket), dispatcher_(dispatcher) {}
  Network::ConnectionSocket& socket() override { return socket_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void continueFilterChain(bool success) override { RELEASE_ASSERT(success, ""); }

  Network::ConnectionSocket& socket_;
  Event::Dispatcher& dispatcher_;
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
  FastMockOsSysCalls(const std::vector<uint8_t>& headers) : headers_(headers) {}

  Api::SysCallSizeResult recv(int, void* buffer, size_t length, int) override {
    RELEASE_ASSERT(length >= headers_.size(), "");
    memcpy(buffer, headers_.data(), headers_.size());
    return Api::SysCallSizeResult{ssize_t(headers_.size()), 0};
  }

  const std::vector<uint8_t> headers_;
};

static void BM_Http1xInspector(benchmark::State& state) {
  absl::string_view header =
      "GET /anything HTTP/1.1\r\nhost: google.com\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-forwarded-proto: http\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";
  std::vector<uint8_t> data(header.begin(), header.end());

  NiceMock<FastMockOsSysCalls> os_sys_calls(data);
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls};
  NiceMock<Stats::MockStore> store;
  ConfigSharedPtr config(std::make_shared<Config>(store));
  Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>();
  Network::ConnectionSocketImpl socket(std::move(io_handle), nullptr, nullptr);
  NiceMock<FastMockDispatcher> dispatcher;
  FastMockListenerFilterCallbacks cb(socket, dispatcher);

  for (auto _ : state) {
    Filter filter(config);
    filter.onAccept(cb);
    dispatcher.file_event_callback_(Event::FileReadyType::Read);
    RELEASE_ASSERT(socket.requestedApplicationProtocols().size() == 1 &&
                       socket.requestedApplicationProtocols().front() == "http/1.1",
                   "");
    socket.setRequestedApplicationProtocols({});
  }
}

static void BM_Http2Inspector(benchmark::State& state) {
  std::string header =
      "505249202a20485454502f322e300d0a0d0a534d0d0a0d0a00000c04000000000000041000000000020000000000"
      "00040800000000000fff000100007d010500000001418aa0e41d139d09b8f0000f048860757a4ce6aa660582867a"
      "8825b650c3abb8d2e053032a2f2a408df2b4a7b3c0ec90b22d5d8749ff839d29af4089f2b585ed6950958d279a18"
      "9e03f1ca5582265f59a75b0ac3111959c7e49004908db6e83f4096f2b16aee7f4b17cd65224b22d6765926a4a7b5"
      "2b528f840b60003f";
  std::vector<uint8_t> data = Hex::decode(header);
  NiceMock<FastMockOsSysCalls> os_sys_calls(data);
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls};
  NiceMock<Stats::MockStore> store;
  ConfigSharedPtr config(std::make_shared<Config>(store));
  Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>();
  Network::ConnectionSocketImpl socket(std::move(io_handle), nullptr, nullptr);
  NiceMock<FastMockDispatcher> dispatcher;
  FastMockListenerFilterCallbacks cb(socket, dispatcher);

  for (auto _ : state) {
    Filter filter(config);
    filter.onAccept(cb);
    dispatcher.file_event_callback_(Event::FileReadyType::Read);
    RELEASE_ASSERT(socket.requestedApplicationProtocols().size() == 1 &&
                       socket.requestedApplicationProtocols().front() == "h2",
                   "");
    socket.setRequestedApplicationProtocols({});
  }
}

BENCHMARK(BM_Http1xInspector)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Http2Inspector)->Unit(benchmark::kMicrosecond);

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_context(spdlog::level::warn,
                                         Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
