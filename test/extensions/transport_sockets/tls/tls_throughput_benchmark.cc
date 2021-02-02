#include "common/buffer/buffer_impl.h"

#include "test/test_common/environment.h"

#include "benchmark/benchmark.h"
#include "openssl/ssl.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {
namespace Extensions::TransportSockets::Tls {

static void drainErrorQueue() {
  while (uint64_t err = ERR_get_error()) {
    std::string failure_reason =
        absl::StrCat(err, ":", ERR_lib_error_string(err), ":", ERR_func_error_string(err), ":",
                     ERR_reason_error_string(err));
    ENVOY_LOG_MISC(error, "{}", failure_reason);
  }
}

static void handleSslError(SSL* ssl, int err, bool is_server) {
  int error = SSL_get_error(ssl, err);
  switch (error) {
  case SSL_ERROR_NONE:
  case SSL_ERROR_WANT_READ:
  case SSL_ERROR_WANT_WRITE:
    return;
  default:
    drainErrorQueue();
    ENVOY_LOG_MISC(error, "is_server {} handshake err {} SSL_get_error {}", is_server, err, error);
    PANIC("Unexpected error during handshake");
  }
}

static void appendSlice(Buffer::Instance& buffer, uint32_t size) {
  std::string data(size, 'a');
  RELEASE_ASSERT(data.size() <= 16384, "short_slice_size can't be larger than full slice");

  // A 16kb request currently has inline metadata, which makes it 16384+8. This gets rounded up
  // to the next page size. Request enough that there is no extra space, to ensure that this results
  // in a new slice.
  auto reservation = buffer.reserveSingleSlice(16384);

  memcpy(reservation.slice().mem_, data.data(), data.size());
  reservation.commit(data.size());
}

// If move_slices is true, add full-sized slices using move similar to how HTTP codecs move data
// from the filter chain buffer to the output buffer. Else, append full-sized slices directly to the
// output buffer like socket read would do.
static void addFullSlices(Buffer::Instance& output_buffer, unsigned num_slices, bool move_slices) {
  Buffer::OwnedImpl tmp_buf;
  Buffer::Instance* buffer = move_slices ? &tmp_buf : &output_buffer;

  const auto initial_slices = buffer->getRawSlices().size();
  while ((buffer->getRawSlices().size() - initial_slices) < num_slices) {
    Buffer::Reservation reservation = buffer->reserveForRead();
    memset(reservation.slices()[0].mem_, 'a', reservation.slices()[0].len_);
    reservation.commit(reservation.slices()[0].len_);
  }

  if (move_slices) {
    output_buffer.move(tmp_buf);
  }
}

static void testThroughput(benchmark::State& state) {
  std::string error;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create("tls_throughput_benchmark", &error));
  Envoy::TestEnvironment::setRunfiles(runfiles.get());

  int sockets[2];
  socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets);

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));
  std::string cert_path = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem");
  std::string key_path = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem");
  auto err = SSL_CTX_use_certificate_file(server_ctx.get(), cert_path.c_str(), SSL_FILETYPE_PEM);
  drainErrorQueue();
  RELEASE_ASSERT(err > 0, "SSL_CTX_use_certificate_file");
  err = SSL_CTX_use_PrivateKey_file(server_ctx.get(), key_path.c_str(), SSL_FILETYPE_PEM);
  RELEASE_ASSERT(err > 0, "SSL_CTX_use_PrivateKey_file");

  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  SSL_set_fd(server_ssl.get(), sockets[0]);
  SSL_set_accept_state(server_ssl.get());

  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  SSL_set_fd(client_ssl.get(), sockets[1]);
  SSL_set_connect_state(client_ssl.get());

  bool handshake_success = false;
  for (int i = 0; i < 50; i++) {
    int client_err = SSL_do_handshake(client_ssl.get());
    int server_err = SSL_do_handshake(server_ssl.get());
    if (client_err == 1 && server_err == 1) {
      handshake_success = true;
      break;
    }
    handleSslError(client_ssl.get(), client_err, false);
    handleSslError(server_ssl.get(), server_err, true);
  }

  RELEASE_ASSERT(handshake_success, "handshake completed successfully");

  static uint8_t read_buf[1024 * 1024];

  unsigned short_slice_size = state.range(0);
  unsigned num_short_slices = state.range(1);
  unsigned align_to_16kb = state.range(2);
  unsigned move_slices = state.range(3);

  uint64_t bytes_written = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    state.PauseTiming();

    // Empty out the read side to make space for the writes.
    while (SSL_read(server_ssl.get(), read_buf, sizeof(read_buf)) > 0) {
    }

    Buffer::OwnedImpl write_buf;
    for (unsigned i = 0; i < num_short_slices; i++) {
      appendSlice(write_buf, short_slice_size);
    }
    if (align_to_16kb) {
      appendSlice(write_buf, 16384 - (num_short_slices * short_slice_size));
      RELEASE_ASSERT(write_buf.length() == 16384,
                     fmt::format("expected length 16384, got {}", write_buf.length()));
      RELEASE_ASSERT(write_buf.getRawSlices().size() == (num_short_slices + 1),
                     fmt::format("buffer number of slices expected {}, got {}",
                                 num_short_slices + 1, write_buf.getRawSlices().size()));
    } else {
      RELEASE_ASSERT(write_buf.length() == (num_short_slices * short_slice_size),
                     fmt::format("expected length {}, got {}", num_short_slices * short_slice_size,
                                 write_buf.length()));
      RELEASE_ASSERT(write_buf.getRawSlices().size() == num_short_slices,
                     fmt::format("buffer number of slices expected {}, got {}", num_short_slices,
                                 write_buf.getRawSlices().size()));
    }

    addFullSlices(write_buf, 10, move_slices);
    bytes_written += write_buf.length();

    state.ResumeTiming();
    uint32_t num_writes = 0;
    uint32_t num_times_linearize_did_something = 0;
    while (write_buf.length() > 0) {
      const Buffer::RawSlice initial = write_buf.frontSlice();
      void* mem;
      size_t len = std::min<uint64_t>(write_buf.length(), 16384);
      mem = write_buf.linearize(len);
      if (write_buf.frontSlice() != initial) {
        ++num_times_linearize_did_something;
      }

      err = SSL_write(client_ssl.get(), mem, len);
      RELEASE_ASSERT(err == static_cast<int>(len),
                     absl::StrCat("SSL_write got: ", err, " expected: ", len));
      write_buf.drain(len);
      num_writes++;
    }

    state.counters["writes_per_iteration"] = num_writes;
    state.counters["num_linearized"] = num_times_linearize_did_something;
  }
  state.counters["throughput"] = benchmark::Counter(bytes_written, benchmark::Counter::kIsRate);

  ::close(sockets[0]);
  ::close(sockets[1]);
}

static void testParams(benchmark::internal::Benchmark* b) {
  for (auto move_slices : {false, true}) {
    for (auto align_to_16kb : {false, true}) {
      // Add a single case of no short slices; don't iterate over the sizes
      // which duplicates test cases when count is zero.
      b->Args({0, 0, align_to_16kb, move_slices});

      for (auto short_slice_size : {1, 128, 4095, 4096, 4097}) {
        for (auto num_short_slices : {1, 2, 3}) {
          b->Args({short_slice_size, num_short_slices, align_to_16kb, move_slices});
        }
      }
    }
  }
}

BENCHMARK(testThroughput)->Unit(::benchmark::kMicrosecond)->Apply(testParams);

} // namespace Extensions::TransportSockets::Tls
} // namespace Envoy
