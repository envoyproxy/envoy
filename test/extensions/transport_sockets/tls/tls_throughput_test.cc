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

static void appendSlice(Buffer::Instance& buffer, uint32_t size) {
  Buffer::RawSlice slice;
  std::string data(size, 'a');
  RELEASE_ASSERT(data.size() <= 16384, "short_slice_size can't be larger than full slice");

  // A 16kb request currently has inline metadata, which makes it 16384+8. This gets rounded up
  // to the next page size. Request enough that there is no extra space, to ensure that this results
  // in a new slice.
  buffer.reserve(20416, &slice, 1);

  memcpy(slice.mem_, data.data(), data.size());
  slice.len_ = data.size();
  buffer.commit(&slice, 1);
}

static void testThroughput(benchmark::State& state) {
  std::string error;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create("tls_throughput_test", &error));
  Envoy::TestEnvironment::setRunfiles(runfiles.get());

  int sockets[2];
  socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets);

  auto* server_ctx = SSL_CTX_new(TLS_method());
  auto* client_ctx = SSL_CTX_new(TLS_method());
  std::string cert_path = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem");
  std::string key_path = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem");
  auto err = SSL_CTX_use_certificate_file(server_ctx, cert_path.c_str(), SSL_FILETYPE_PEM);
  drainErrorQueue();
  RELEASE_ASSERT(err > 0, "SSL_CTX_use_certificate_file");
  err = SSL_CTX_use_PrivateKey_file(server_ctx, key_path.c_str(), SSL_FILETYPE_PEM);
  RELEASE_ASSERT(err > 0, "SSL_CTX_use_PrivateKey_file");

  SSL* server_ssl = SSL_new(server_ctx);
  SSL_set_fd(server_ssl, sockets[0]);
  SSL_set_accept_state(server_ssl);

  SSL* client_ssl = SSL_new(client_ctx);
  SSL_set_fd(client_ssl, sockets[1]);
  SSL_set_connect_state(client_ssl);

  bool handshake_success = false;
  for (int i = 0; i < 50; i++) {
    int client_err = SSL_do_handshake(client_ssl);
    int server_err = SSL_do_handshake(server_ssl);
    if (client_err == 1 && server_err == 1) {
      handshake_success = true;
      break;
    }
    int err = SSL_get_error(server_ssl, server_err);
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      continue;
    default:
      drainErrorQueue();
      PANIC("Unexpected error during handshake");
    }

    ENVOY_LOG_MISC(error, "client_err {} server_err {}", client_err, server_err);
  }

  RELEASE_ASSERT(handshake_success, "handshake completed successfully");

  static uint8_t read_buf[1024 * 1024];

  auto full_linearize = state.range(0);
  unsigned short_slice_size = state.range(1);
  unsigned num_short_slices = state.range(2);

  uint64_t bytes_written = 0;
  for (auto _ : state) {
    state.PauseTiming();

    // Empty out the read side to make space for the writes.
    while (SSL_read(server_ssl, read_buf, sizeof(read_buf)) > 0)
      ;

    std::string send(short_slice_size, 'a');
    Buffer::OwnedImpl write_buf;
    for (unsigned i = 0; i < num_short_slices; i++) {
      if (short_slice_size > 0) {
        appendSlice(write_buf, short_slice_size);
      }
    }
    appendSlice(write_buf, 16384 - (num_short_slices * short_slice_size));
    RELEASE_ASSERT(write_buf.length() == 16384,
                   fmt::format("expected length 16384, got {}", write_buf.length()));
    RELEASE_ASSERT(write_buf.getRawSlices().size() == (num_short_slices + 1),
                   fmt::format("buffer number of slices expected {}, got {}", num_short_slices + 1,
                               write_buf.getRawSlices().size()));

    // Append many full-sized slices, the same manner that an envoy socket read would.
    for (unsigned i = 0; i < 10; i++) {
      auto start_size = write_buf.length();
      Buffer::RawSlice slices[2];
      auto num_slices = write_buf.reserve(16384, slices, 2);
      for (unsigned i = 0; i < num_slices; i++) {
        memset(slices[i].mem_, 'a', slices[i].len_);
      }
      write_buf.commit(slices, 2);
      RELEASE_ASSERT(write_buf.length() - start_size == 16384, "correct reserve/commit");
    }

    bytes_written += write_buf.length();

    state.ResumeTiming();
    uint32_t num_writes = 0;
    while (write_buf.length() > 0) {
      void* mem;
      size_t len = std::min<uint64_t>(write_buf.length(), 16384);
      if (full_linearize) {
        mem = write_buf.linearize(len);
      } else {
        auto slice = write_buf.maybeLinearize(len, 4096);
        mem = slice.mem_;
        len = slice.len_;
      }

      err = SSL_write(client_ssl, mem, len);
      RELEASE_ASSERT(err == static_cast<int>(len), "SSL_write");
      write_buf.drain(len);
      num_writes++;
    }

    state.counters["writes_per_iteration"] = num_writes;
  }
  state.counters["throughput"] = benchmark::Counter(bytes_written, benchmark::Counter::kIsRate);
}

static void TestParams(benchmark::internal::Benchmark* b) {
  // Add a single case of no short slices; don't iterate over the sizes
  // which duplicates test cases when count is zero.
  b->Args({false, 0, 0});
  b->Args({true, 0, 0});

  for (auto num_short_slices : {1, 2, 3}) {
    for (auto short_slice_size : {1, 128, 4096}) {
      b->Args({false, short_slice_size, num_short_slices});
      b->Args({true, short_slice_size, num_short_slices});
    }
  }
}

BENCHMARK(testThroughput)->Unit(::benchmark::kMicrosecond)->Apply(TestParams);

} // namespace Extensions::TransportSockets::Tls
} // namespace Envoy
