// Measures the per-write overhead the tap transport socket adds on top of the socket it wraps.

#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/transport_sockets/tap/tap.h"

#include "test/mocks/network/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {
namespace {

// Inner transport socket that accepts every byte offered but does not drain the buffer.
// This allows write benchmarks to reuse the same buffer across iterations
// without paying to refill it.
class NullTransportSocket : public Network::TransportSocket {
public:
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks&) override {}
  std::string protocol() const override { return EMPTY_STRING; }
  absl::string_view failureReason() const override { return EMPTY_STRING; }
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent, bool) override {}
  Network::IoResult doRead(Buffer::Instance&) override {
    return {Network::PostIoAction::KeepOpen, 0, false};
  }
  Network::IoResult doWrite(Buffer::Instance& buffer, bool) override {
    return {Network::PostIoAction::KeepOpen, buffer.length(), false};
  }
  void onConnected() override {}
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return true; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
};

// Tapper that consumes the snapshot without doing any trace work, isolating the cost the socket
// itself imposes from the cost of the sink.
class NullPerSocketTapper : public PerSocketTapper {
public:
  void closeSocket(Network::ConnectionEvent) override {}
  void onRead(const Buffer::Instance& data, uint32_t bytes_read) override {
    benchmark::DoNotOptimize(data.length());
    benchmark::DoNotOptimize(bytes_read);
  }
  void onWrite(const Buffer::Instance& data, uint32_t bytes_written, bool) override {
    benchmark::DoNotOptimize(data.length());
    benchmark::DoNotOptimize(bytes_written);
  }
};

// Config that admits the connection and hands back a NullPerSocketTapper.
class NullSocketTapConfig : public SocketTapConfig {
public:
  PerSocketTapperPtr
  createPerSocketTapper(const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig&,
                        const TransportTapStats&, const Network::Connection&) override {
    return std::make_unique<NullPerSocketTapper>();
  }
  TimeSource& timeSource() const override { PANIC("not implemented"); }

  // Extensions::Common::Tap::TapConfig
  Extensions::Common::Tap::PerTapSinkHandleManagerPtr
  createPerTapSinkHandleManager(uint64_t) override {
    PANIC("not implemented");
  }
  uint32_t maxBufferedRxBytes() const override { PANIC("not implemented"); }
  uint32_t maxBufferedTxBytes() const override { PANIC("not implemented"); }
  uint32_t minStreamedSentBytes() const override { PANIC("not implemented"); }
  Extensions::Common::Tap::Matcher::MatchStatusVector createMatchStatusVector() const override {
    PANIC("not implemented");
  }
  const Extensions::Common::Tap::Matcher& rootMatcher() const override { PANIC("not implemented"); }
  bool streaming() const override { PANIC("not implemented"); }
  bool shouldRecord() const override { return true; }
};

// Holds everything a TapSocket needs to be driven outside of a real connection.
class Harness {
public:
  Harness(SocketTapConfigSharedPtr config, uint64_t buffer_size)
      : socket_(std::move(config), socket_tap_config_, *stats_store_.rootScope(),
                std::make_unique<NullTransportSocket>()) {
    socket_.setTransportSocketCallbacks(callbacks_);
    buffer_.add(std::string(buffer_size, 'a'));
  }

  Network::TransportSocket& socket() { return socket_; }
  Buffer::Instance& buffer() { return buffer_; }

private:
  Stats::IsolatedStoreImpl stats_store_;
  const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig socket_tap_config_;
  testing::NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  TapSocket socket_;
  Buffer::OwnedImpl buffer_;
};

void doWriteBenchmark(benchmark::State& state, SocketTapConfigSharedPtr config) {
  const uint64_t buffer_size = state.range(0);
  Harness harness(std::move(config), buffer_size);

  uint64_t bytes = 0;
  for (auto _ : state) { // NOLINT
    UNREFERENCED_PARAMETER(_);
    bytes += harness.socket().doWrite(harness.buffer(), false).bytes_processed_;
  }
  benchmark::DoNotOptimize(bytes);
  state.SetBytesProcessed(state.iterations() * buffer_size);
}

// No tap configured (or sampling rejected the connection): the wrapper is pure overhead.
void tapSocketDoWriteNoTapper(benchmark::State& state) { doWriteBenchmark(state, nullptr); }
BENCHMARK(tapSocketDoWriteNoTapper)
    ->Arg(0)
    ->Arg(1024)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144)
    ->Arg(1048576);

// Tap active: the snapshot is required, so this arm should not move.
void tapSocketDoWriteWithTapper(benchmark::State& state) {
  doWriteBenchmark(state, std::make_shared<NullSocketTapConfig>());
}
BENCHMARK(tapSocketDoWriteWithTapper)
    ->Arg(0)
    ->Arg(1024)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144)
    ->Arg(1048576);

} // namespace
} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
