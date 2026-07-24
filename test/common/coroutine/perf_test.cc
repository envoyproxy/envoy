// Micro-benchmark contrasting a callback-based echo server against a
// coroutine-based echo server, both running over Envoy's real Event::Dispatcher
// and Network::IoHandle. The two servers implement the *same* behavior -- read 4k
// slices up to a high watermark, write as fast as possible -- so the only variable is how
// that control flow is expressed. The coroutine body reads like plain sequential
// I/O (`co_await sock.read(...)` / `co_await sock.write(...)`); the callback version
// open-codes the same policy as an edge-triggered FileReadyCb state machine with
// hand-maintained would-block and watermark bookkeeping.
//
// The load driver is identical for both variants: a pair of blocking threads (one
// writing the payload, one reading the echoes back), so it cannot deadlock against
// the single-threaded server and needs no event-loop bookkeeping of its own.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/io_handle.h"
#include "envoy/thread/thread.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"
#include "source/common/event/libevent.h"
#include "source/common/network/io_socket_handle_impl.h"

#include "test/benchmark/main.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace Envoy {
namespace {

constexpr uint32_t kRead = Event::FileReadyType::Read;
constexpr uint32_t kWrite = Event::FileReadyType::Write;

// Total bytes pushed through the echo loop per benchmark iteration. Large enough
// that steady-state per-event work dominates the fixed per-iteration thread
// spawn/join and coroutine launch cost.
constexpr uint64_t kPayloadBytes = 1 << 20; // 10 MiB.

// Bytes requested per read syscall. Reads happen in fixed-size slices (gated by
// the watermark) rather than sizing each read to the exact room remaining, so the
// buffer may overshoot the watermark by up to one slice -- as in real code.
constexpr uint64_t kReadSize = 4096;

// Which side of the driver is throttled, to model a proxy whose two ends run at
// different speeds. A slow reader lets the server's write buffer back up to the
// high watermark (write backpressure); a slow writer starves it (read waits).
enum class Pace { Balanced, SlowReader, SlowWriter };

// The throttled side pauses this long for every `kThrottleUnit` bytes it transfers
// (independent of the batch size), pacing that side to a few hundred MB/s.
constexpr std::chrono::microseconds kThrottleDelay{300};
constexpr uint64_t kThrottleUnit = 65536;

// ---------------------------------------------------------------------------
// Connected socketpair: the server end is a non-blocking IoHandle driven by the
// dispatcher; the client end is a raw blocking fd handed to the driver threads.
// ---------------------------------------------------------------------------
struct SocketPair {
  SocketPair() {
    os_fd_t fds[2];
    Api::SysCallIntResult rc =
        Api::OsSysCallsSingleton::get().socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    RELEASE_ASSERT(rc.return_value_ == 0, "socketpair() failed");
    server_handle = std::make_unique<Network::IoSocketHandleImpl>(fds[0]);
    server_handle->setBlocking(false);
    client_fd = fds[1]; // Left blocking; driven by the Driver threads.
  }
  ~SocketPair() {
    if (server_handle != nullptr && server_handle->isOpen()) {
      server_handle->close();
    }
    Api::OsSysCallsSingleton::get().close(client_fd);
  }

  Network::IoHandlePtr server_handle;
  os_fd_t client_fd;
};

// ---------------------------------------------------------------------------
// Shared load driver: two blocking threads on the client fd -- one writes the
// N-byte payload in `writer_batch` chunks, one reads the echoes back until it has
// seen N bytes and then fires `on_complete` (used to stop the dispatcher). Because
// the writer and reader are independent threads, kernel socket-buffer backpressure
// provides all the flow control and neither can deadlock the single-threaded
// server. Identical across both server variants.
// ---------------------------------------------------------------------------
class Driver {
public:
  Driver(Thread::ThreadFactory& thread_factory, os_fd_t fd)
      : thread_factory_(thread_factory), fd_(fd) {}

  void start(uint64_t total, uint32_t writer_batch, Pace pace, std::function<void()> on_complete) {
    writer_ = thread_factory_.createThread(
        [this, total, writer_batch, pace]() { writeAll(total, writer_batch, pace); });
    reader_ = thread_factory_.createThread(
        [this, total, pace, on_complete = std::move(on_complete)]() mutable {
          readAll(total, pace);
          on_complete();
        });
  }

  void join() {
    writer_->join();
    reader_->join();
    writer_.reset();
    reader_.reset();
  }

private:
  // Sleep once per `kThrottleUnit` bytes transferred when `slow` is set.
  static void throttle(bool slow, uint64_t& since_pause, uint64_t bytes) {
    if (!slow) {
      return;
    }
    since_pause += bytes;
    if (since_pause >= kThrottleUnit) {
      since_pause = 0;
      // Real wall-clock throttle on a load-generator thread (not Envoy code under
      // test), so a real sleep is intended here.
      std::this_thread::sleep_for(kThrottleDelay); // NO_CHECK_FORMAT(real_time)
    }
  }

  void writeAll(uint64_t total, uint32_t writer_batch, Pace pace) {
    auto& os = Api::OsSysCallsSingleton::get();
    std::vector<char> buf(writer_batch, 'x');
    uint64_t sent = 0;
    uint64_t since_pause = 0;
    while (sent < total) {
      const size_t chunk = std::min<uint64_t>(writer_batch, total - sent);
      size_t off = 0;
      while (off < chunk) {
        Api::SysCallSizeResult r = os.send(fd_, buf.data() + off, chunk - off, MSG_NOSIGNAL);
        if (r.return_value_ < 0) {
          RELEASE_ASSERT(r.errno_ == EINTR, "send() failed");
          continue;
        }
        off += r.return_value_;
      }
      sent += chunk;
      throttle(pace == Pace::SlowWriter, since_pause, chunk);
    }
  }

  void readAll(uint64_t total, Pace pace) {
    auto& os = Api::OsSysCallsSingleton::get();
    std::vector<char> buf(65536);
    uint64_t received = 0;
    uint64_t since_pause = 0;
    while (received < total) {
      Api::SysCallSizeResult r = os.recv(fd_, buf.data(), buf.size(), 0);
      if (r.return_value_ < 0) {
        RELEASE_ASSERT(r.errno_ == EINTR, "recv() failed");
        continue;
      }
      RELEASE_ASSERT(r.return_value_ > 0, "peer closed before echo completed");
      received += r.return_value_;
      throttle(pace == Pace::SlowReader, since_pause, r.return_value_);
    }
  }

  Thread::ThreadFactory& thread_factory_;
  os_fd_t fd_;
  Thread::ThreadPtr writer_;
  Thread::ThreadPtr reader_;
};

// ===========================================================================
// Variant 1: callback echo server.
//
// Edge triggered epoll driven echo server. A single read per epoll wake up.
// After the read, starts a write if not blocked.
// If read is blocked at epoll wake up, it skips the read. The write should
// resume the read once it clears the buffer.
//
// One read per iteration is important to not hold up the epoll loop in the
// real world.
// ===========================================================================
class CallbackEchoServer {
public:
  CallbackEchoServer(Event::Dispatcher& dispatcher, Network::IoHandle& io, uint32_t high)
      : io_(io), high_(high) {
    io_.initializeFileEvent(
        dispatcher, [this](uint32_t events) { return onReady(events); },
        Event::FileTriggerType::Edge, kRead | kWrite);
  }

  // Re-arm for a fresh iteration (the socketpair is reused across iterations).
  void arm() {
    RELEASE_ASSERT(buffer_.length() == 0, "incomplete echo");
    RELEASE_ASSERT(write_blocked_ == false, "blocked after echo");
  }

private:
  absl::Status onReady(uint32_t events) {
    if (events & kRead) {
      resumeRead();
    }
    if (events & kWrite) {
      resumeWrite();
    }
    return absl::OkStatus();
  }

  void startRead() {
    if (read_blocked_) {
      return;
    }
    io_.activateFileEvents(Event::FileReadyType::Read);
  }

  void resumeRead() {
    read_blocked_ = false;
    if (buffer_.length() >= high_) {
      return;
    }
    Api::IoCallUint64Result r = io_.read(buffer_, kReadSize);
    if (!r.ok()) {
      RELEASE_ASSERT(r.wouldBlock(), "read() failed");
      ++read_blocks_;
      read_blocked_ = true;
      return;
    }
    RELEASE_ASSERT(r.return_value_ > 0, "peer closed before echo completed");
    startWrite();
  }

  void startWrite() {
    if (write_blocked_) {
      return;
    }
    // directly resume write to aviod an addition epoll trip.
    resumeWrite();
  }

  void resumeWrite() {
    write_blocked_ = false;
    Api::IoCallUint64Result w = io_.write(buffer_);
    if (!w.ok()) {
      RELEASE_ASSERT(w.wouldBlock(), "write() failed");
      ++write_blocks_;
      write_blocked_ = true;
      return;
    }
    startRead();
  }

public:
  // Cumulative event-loop waits across all iterations, per direction.
  uint64_t readBlocks() const { return read_blocks_; }
  uint64_t writeBlocks() const { return write_blocks_; }

private:
  Network::IoHandle& io_;
  Buffer::OwnedImpl buffer_;
  const uint32_t high_;
  bool read_blocked_ = 0;
  bool write_blocked_ = 0;
  uint64_t read_blocks_ = 0;
  uint64_t write_blocks_ = 0;
};

// ===========================================================================
// Variant 2: coroutine echo server.
//
// One read per epoll iteration as well. The end to end behavior is the same
// as the callback server.
// ===========================================================================

// SocketReady, YieldToNextIeration and AsyncSocket should be provided by the
// base library, but it doesn't yet exist.
class SocketReady;
class YieldToNextIteration;

class AsyncSocket {
public:
  AsyncSocket(Event::Dispatcher& dispatcher, Network::IoHandle& io) : io_(io) {
    io_.initializeFileEvent(
        dispatcher,
        [this](uint32_t events) {
          onReady(events);
          return absl::OkStatus();
        },
        Event::FileTriggerType::Edge, kRead | kWrite);
    cb_for_yield = dispatcher.createSchedulableCallback([this] { resumeFromYield(); });
  }

  Coroutine::Task<absl::StatusOr<uint64_t>> read(Buffer::Instance& buf, uint64_t max,
                                                 bool await = true);

  Coroutine::Task<absl::StatusOr<uint64_t>> write(Buffer::Instance& buf);

  // Cumulative event-loop waits across all iterations, per direction.
  uint64_t readBlocks() const { return read_blocks_; }
  uint64_t writeBlocks() const { return write_blocks_; }

private:
  friend class SocketReady;
  friend class YieldToNextIteration;

  SocketReady whenReady(uint32_t event);
  void onReady(uint32_t events);
  void resumeFromYield();

  Network::IoHandle& io_;
  Buffer::OwnedImpl write_slice_;
  SocketReady* waiter_ = nullptr;
  YieldToNextIteration* yielder_ = nullptr;
  uint32_t wanted_ = 0;
  uint64_t read_blocks_ = 0;
  uint64_t write_blocks_ = 0;

  Event::SchedulableCallbackPtr cb_for_yield = nullptr;
};

class SocketReady : public Coroutine::LeafAwaitable<absl::Status> {
public:
  SocketReady(AsyncSocket& sock, uint32_t event) : sock_(sock), event_(event) {}

  void deliver() { complete(absl::OkStatus()); }

protected:
  void onStart() override {
    sock_.waiter_ = this;
    sock_.wanted_ = event_;
  }
  void onCancel() override { sock_.waiter_ = nullptr; }

private:
  AsyncSocket& sock_;
  const uint32_t event_;
};

class YieldToNextIteration : public Coroutine::LeafAwaitable<absl::Status> {
public:
  YieldToNextIteration(AsyncSocket& sock) : sock_(sock) {}
  void resume() { complete(absl::OkStatus()); }

private:
  void onStart() override {
    sock_.yielder_ = this;
    sock_.cb_for_yield->scheduleCallbackNextIteration();
  }
  void onCancel() override { sock_.yielder_ = nullptr; }
  AsyncSocket& sock_;
};

SocketReady AsyncSocket::whenReady(uint32_t event) { return SocketReady(*this, event); }

void AsyncSocket::onReady(uint32_t events) {
  if (waiter_ != nullptr && (events & wanted_) != 0) {
    SocketReady* w = waiter_;
    waiter_ = nullptr;
    w->deliver();
  }
}

void AsyncSocket::resumeFromYield() {
  if (yielder_ != nullptr) {
    YieldToNextIteration* y = yielder_;
    yielder_ = nullptr;
    y->resume();
  }
}

Coroutine::Task<absl::StatusOr<uint64_t>> AsyncSocket::read(Buffer::Instance& buf, uint64_t max,
                                                            bool await) {
  while (true) {
    Api::IoCallUint64Result r = io_.read(buf, max);
    if (r.ok()) {
      co_return r.return_value_;
    }
    if (!r.wouldBlock()) {
      co_return absl::InternalError("read() failed");
    }
    if (!await) {
      co_return 0;
    }
    ++read_blocks_;
    if (absl::Status s = co_await whenReady(kRead); !s.ok()) {
      co_return s;
    }
  }
}

Coroutine::Task<absl::StatusOr<uint64_t>> AsyncSocket::write(Buffer::Instance& buf) {
  while (true) {
    Api::IoCallUint64Result r = io_.write(buf); // Drains the written bytes.
    if (r.ok()) {
      co_return r.return_value_;
    }
    if (!r.wouldBlock()) {
      co_return absl::InternalError("write() failed");
    }
    ++write_blocks_;
    if (absl::Status s = co_await whenReady(kWrite); !s.ok()) {
      co_return s;
    }
  }
}

// This is the real coroutine.
Coroutine::Task<absl::Status> coroEcho(AsyncSocket& sock, uint32_t high) {
  Buffer::OwnedImpl buf;
  while (true) {
    if (buf.length() < high) {
      bool await = buf.length() == 0;
      absl::StatusOr<uint64_t> n = co_await sock.read(buf, kReadSize, await);
      if (!n.ok()) {
        co_return n.status();
      }
      if (*n == 0 && await) {
        co_return absl::InternalError("peer closed before echo completed");
      }
    }
    if (buf.length() > 0) {
      absl::StatusOr<uint64_t> wrote = co_await sock.write(buf);
      if (!wrote.ok()) {
        co_return wrote.status();
      }
    }
    if (absl::Status s = co_await YieldToNextIteration(sock); !s.ok()) {
      co_return s;
    }
  }
  co_return absl::OkStatus();
}

// ---------------------------------------------------------------------------
// Benchmarks.
// ---------------------------------------------------------------------------
uint64_t payloadBytes() {
  return Envoy::benchmark::skipExpensiveBenchmarks() ? (1 << 16) : kPayloadBytes;
}

// The benchmark main does not initialize libevent's thread support (the
// DispatcherImpl ctor asserts on it); do it once, lazily.
void ensureLibeventInitialized() {
  static const bool initialized = []() {
    Event::Libevent::Global::initialize();
    return true;
  }();
  (void)initialized;
}

void reportBlocks(::benchmark::State& state, uint64_t read_blocks, uint64_t write_blocks) {
  state.counters["read_blk"] =
      ::benchmark::Counter(read_blocks, ::benchmark::Counter::kAvgIterations);
  state.counters["write_blk"] =
      ::benchmark::Counter(write_blocks, ::benchmark::Counter::kAvgIterations);
}

void bmCallbackEcho(::benchmark::State& state, Pace pace) {
  const uint32_t writer_batch = state.range(0);
  const uint32_t high = state.range(1);
  const uint64_t total = payloadBytes();
  ensureLibeventInitialized();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("bench");
  SocketPair sockets;
  CallbackEchoServer server(*dispatcher, *sockets.server_handle, high);
  Driver driver(api->threadFactory(), sockets.client_fd);

  for (auto _ : state) { // NOLINT: Silences warning about unused variable.
    server.arm();
    driver.start(total, writer_batch, pace,
                 [&dispatcher]() { dispatcher->post([&dispatcher]() { dispatcher->exit(); }); });
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    driver.join();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(total));
  reportBlocks(state, server.readBlocks(), server.writeBlocks());
}

void bmCoroutineEcho(::benchmark::State& state, Pace pace) {
  const uint32_t writer_batch = state.range(0);
  const uint32_t high = state.range(1);
  const uint64_t total = payloadBytes();
  ensureLibeventInitialized();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("bench");
  SocketPair sockets;
  auto executor = std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher);
  AsyncSocket sock(*dispatcher, *sockets.server_handle);
  Coroutine::DetachedHandle handle = Coroutine::launch(
      coroEcho(sock, high), executor,
      [](absl::Status status) { RELEASE_ASSERT(absl::IsCancelled(status), "echo"); },
      Coroutine::StartMode::Inline);
  Driver driver(api->threadFactory(), sockets.client_fd);

  for (auto _ : state) { // NOLINT: Silences warning about unused variable.
    driver.start(total, writer_batch, pace,
                 [&dispatcher]() { dispatcher->post([&dispatcher]() { dispatcher->exit(); }); });
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    driver.join();
  }
  handle.cancel();
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(total));
  reportBlocks(state, sock.readBlocks(), sock.writeBlocks());
}

// Balanced: full writer_batch x high grid. Slow-reader / slow-writer: the throttle
// dominates throughput, so fix writer_batch (16k) and sweep the high watermark to
// show how it governs how far the buffer backs up (or how often it starves).
BENCHMARK_CAPTURE(bmCallbackEcho, balanced, Pace::Balanced)
    ->Ranges({{1 << 10, 1 << 16}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bmCallbackEcho, slow_reader, Pace::SlowReader)
    ->Ranges({{1 << 14, 1 << 14}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bmCallbackEcho, slow_writer, Pace::SlowWriter)
    ->Ranges({{1 << 12, 1 << 16}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK_CAPTURE(bmCoroutineEcho, balanced, Pace::Balanced)
    ->Ranges({{1 << 10, 1 << 16}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bmCoroutineEcho, slow_reader, Pace::SlowReader)
    ->Ranges({{1 << 14, 1 << 14}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bmCoroutineEcho, slow_writer, Pace::SlowWriter)
    ->Ranges({{1 << 12, 1 << 16}, {1 << 10, 1 << 16}})
    ->Unit(::benchmark::kMicrosecond);

} // namespace
} // namespace Envoy
