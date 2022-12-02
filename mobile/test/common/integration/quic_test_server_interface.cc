#include "test/common/integration/quic_test_server_interface.h"

// NOLINT(namespace-envoy)

static std::shared_ptr<Envoy::QuicTestServer> strong_quic_test_server_;
static std::weak_ptr<Envoy::QuicTestServer> weak_quic_test_server_;

static std::shared_ptr<Envoy::QuicTestServer> quic_test_server() {
  return weak_quic_test_server_.lock();
}

void start_server() {
  strong_quic_test_server_ = std::make_shared<Envoy::QuicTestServer>();
  weak_quic_test_server_ = strong_quic_test_server_;

  if (auto e = quic_test_server()) {
    e->startQuicTestServer();
  }
}

void shutdown_server() {
  // Reset the primary handle to the test_server,
  // but retain it long enough to synchronously shutdown.
  auto e = strong_quic_test_server_;
  strong_quic_test_server_.reset();
  e->shutdownQuicTestServer();
}

int get_server_port() {
  if (auto e = quic_test_server()) {
    return e->getServerPort();
  }
  return -1; // failure
}
