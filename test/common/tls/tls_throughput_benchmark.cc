#include <cerrno>
#include <string>

#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/cleanup.h"
#include "source/common/event/libevent.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
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
      bazel::tools::cpp::runfiles::Runfiles::Create("tls_throughput_benchmark",
                                                    BAZEL_CURRENT_REPOSITORY, &error));
  Envoy::TestEnvironment::setRunfiles(runfiles.get());

  int sockets[2];
  socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets);

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));
  std::string cert_path =
      Envoy::TestEnvironment::runfilesPath("test/common/tls/test_data/san_dns_cert.pem");
  std::string key_path =
      Envoy::TestEnvironment::runfilesPath("test/common/tls/test_data/san_dns_key.pem");
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

class ReceiveCountingIoHandle : public Network::IoSocketHandleImpl {
public:
  explicit ReceiveCountingIoHandle(os_fd_t fd) : IoSocketHandleImpl(fd) {}

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override {
    ++num_reads_;
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  uint64_t numReads() const { return num_reads_; }

private:
  uint64_t num_reads_{0};
};

// Measure the production TLS transport socket's receive path with a full batch already queued.
// Encryption, sending, handshakes, and payload verification are excluded from the timed region.
static void testReceiveThroughput(benchmark::State& state) {
  std::string error;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create("tls_throughput_benchmark",
                                                    BAZEL_CURRENT_REPOSITORY, &error));
  TestEnvironment::setRunfiles(runfiles.get());

  int sockets[2];
  RELEASE_ASSERT(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) == 0, "socketpair");
  auto receiver = std::make_unique<ReceiveCountingIoHandle>(sockets[0]);
  const auto& receiver_io_handle = *receiver;
  Network::IoSocketHandleImpl sender(sockets[1]);
  const int send_buffer_size = 1024 * 1024;
  if (setsockopt(sockets[1], SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) !=
      0) {
    state.SkipWithError("Cannot enlarge the socket send buffer for a full ciphertext batch");
    return;
  }

  const int tls_version = state.range(0) == 12 ? TLS1_2_VERSION : TLS1_3_VERSION;
  const size_t record_size = state.range(1);
  const uint32_t read_ahead_size = state.range(2);
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  if (!Event::Libevent::Global::initialized()) {
    Event::Libevent::Global::initialize();
  }
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("tls_receive_benchmark");
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  ON_CALL(factory_context.server_context_, api()).WillByDefault(testing::ReturnRef(*api));
  ON_CALL(factory_context.server_context_, serverScope())
      .WillByDefault(testing::ReturnRef(*stats_store.rootScope()));
  ContextManagerImpl manager(factory_context.serverFactoryContext());

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto& common_context = *tls_context.mutable_common_tls_context();
  common_context.mutable_read_ahead_buffer_size()->set_value(read_ahead_size);
  using TlsParameters = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  const auto protocol = state.range(0) == 12 ? TlsParameters::TLSv1_2 : TlsParameters::TLSv1_3;
  common_context.mutable_tls_params()->set_tls_minimum_protocol_version(protocol);
  common_context.mutable_tls_params()->set_tls_maximum_protocol_version(protocol);
  auto* certificate = common_context.add_tls_certificates();
  certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/common/tls/test_data/san_dns_cert.pem"));
  certificate->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/common/tls/test_data/san_dns_key.pem"));
  auto config = ServerContextConfigImpl::create(tls_context, factory_context, {}, false);
  RELEASE_ASSERT(config.ok(), config.status().ToString());
  auto factory =
      ServerSslSocketFactory::create(std::move(*config), manager, *stats_store.rootScope());
  RELEASE_ASSERT(factory.ok(), factory.status().ToString());

  // The anonymous socketpair needs an address only for connection diagnostics.
  auto address = Network::Address::PipeInstance::create("tls_receive_benchmark");
  RELEASE_ASSERT(address.ok(), address.status().ToString());
  Network::Address::InstanceConstSharedPtr socket_address = std::move(*address);
  auto socket = std::make_unique<Network::ConnectionSocketImpl>(std::move(receiver), socket_address,
                                                                socket_address);
  StreamInfo::StreamInfoImpl stream_info(dispatcher->timeSource(),
                                         socket->connectionInfoProviderSharedPtr(),
                                         StreamInfo::FilterState::LifeSpan::Connection);
  Network::ConnectionImpl connection(*dispatcher, std::move(socket),
                                     (*factory)->createDownstreamTransportSocket(), stream_info,
                                     true);
  Cleanup close_connection(
      [&connection]() { connection.close(Network::ConnectionCloseType::NoFlush); });
  auto& transport_socket = *connection.transportSocket();
  auto& received = connection.getReadBuffer().buffer;

  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));
  RELEASE_ASSERT(SSL_CTX_set_min_proto_version(client_ctx.get(), tls_version) == 1,
                 "minimum TLS version");
  RELEASE_ASSERT(SSL_CTX_set_max_proto_version(client_ctx.get(), tls_version) == 1,
                 "maximum TLS version");
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  RELEASE_ASSERT(SSL_set_fd(client_ssl.get(), sockets[1]) == 1, "SSL_set_fd");
  SSL_set_connect_state(client_ssl.get());

  bool handshake_success = false;
  for (int i = 0; i < 50; ++i) {
    const int client_err = SSL_do_handshake(client_ssl.get());
    const auto result = transport_socket.doRead(received);
    RELEASE_ASSERT(result.action_ == Network::PostIoAction::KeepOpen && !result.end_stream_read_,
                   std::string(transport_socket.failureReason()));
    if (client_err == 1 && transport_socket.canFlushClose()) {
      handshake_success = true;
      break;
    }
    handleSslError(client_ssl.get(), client_err, false);
  }
  RELEASE_ASSERT(handshake_success, "handshake completed successfully");
  RELEASE_ASSERT(SSL_version(client_ssl.get()) == tls_version, "negotiated TLS version");
  RELEASE_ASSERT(received.length() == 0, "handshake produced no application data");
  state.SetLabel(SSL_CIPHER_get_name(SSL_get_current_cipher(client_ssl.get())));

  BIO* output = BIO_new(BIO_s_mem());
  RELEASE_ASSERT(output != nullptr, "BIO_new");
  SSL_set0_wbio(client_ssl.get(), output);
  const std::string plaintext(256 * 1024, 'a');

  const auto queue_batch = [&]() {
    for (size_t offset = 0; offset < plaintext.size(); offset += record_size) {
      RELEASE_ASSERT(SSL_write(client_ssl.get(), plaintext.data() + offset, record_size) ==
                         static_cast<int>(record_size),
                     "SSL_write");
    }
    const uint8_t* ciphertext = nullptr;
    size_t ciphertext_length = 0;
    RELEASE_ASSERT(BIO_mem_contents(output, &ciphertext, &ciphertext_length) == 1,
                   "BIO_mem_contents");
    size_t sent = 0;
    while (sent < ciphertext_length) {
      const ssize_t result = ::send(sockets[1], ciphertext + sent, ciphertext_length - sent, 0);
      if (result < 0 && errno == EINTR) {
        continue;
      }
      if (result <= 0) {
        return false;
      }
      sent += result;
    }
    RELEASE_ASSERT(BIO_reset(output) == 1, "BIO_reset");
    return true;
  };
  const auto receive_batch = [&]() {
    const auto result = transport_socket.doRead(received);
    return result.action_ == Network::PostIoAction::KeepOpen && !result.end_stream_read_ &&
           result.bytes_processed_ == plaintext.size();
  };

  // Warm up the lazy read-ahead allocation before measuring steady-state receive cost.
  if (!queue_batch()) {
    state.SkipWithError("Cannot queue a full ciphertext batch; check socket send buffer capacity");
    return;
  }
  RELEASE_ASSERT(receive_batch() && received.toString() == plaintext, "warmup payload");
  const uint64_t initial_reads = receiver_io_handle.numReads();
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    state.PauseTiming();
    received.drain(received.length());
    if (!queue_batch()) {
      state.SkipWithError(
          "Cannot queue a full ciphertext batch; check socket send buffer capacity");
      break;
    }
    state.ResumeTiming();
    if (!receive_batch()) {
      state.SkipWithError("TLS transport socket did not consume the queued plaintext batch");
      break;
    }
    state.PauseTiming();
    RELEASE_ASSERT(received.toString() == plaintext, "received payload");
    state.ResumeTiming();
  }
  state.SetBytesProcessed(state.iterations() * plaintext.size());
  state.counters["socket_reads_per_batch"] = benchmark::Counter(
      receiver_io_handle.numReads() - initial_reads, benchmark::Counter::kAvgIterations);
}

static void receiveParams(benchmark::internal::Benchmark* b) {
  b->ArgNames({"tls_version", "record_bytes", "read_ahead_bytes"});
  for (const int tls_version : {12, 13}) {
    for (const int record_size : {1024, 16 * 1024}) {
      for (const int read_ahead_size : {0, 16 * 1024, 64 * 1024, 256 * 1024}) {
        b->Args({tls_version, record_size, read_ahead_size});
      }
    }
  }
}

BENCHMARK(testReceiveThroughput)->Unit(::benchmark::kMicrosecond)->Apply(receiveParams);

} // namespace Extensions::TransportSockets::Tls
} // namespace Envoy
