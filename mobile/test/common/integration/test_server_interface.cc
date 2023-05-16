#include "test/common/integration/test_server_interface.h"

#include "extension_registry.h"

// NOLINT(namespace-envoy)

static std::shared_ptr<Envoy::TestServer> strong_test_server_;
static std::weak_ptr<Envoy::TestServer> weak_test_server_;

static std::shared_ptr<Envoy::TestServer> test_server() { return weak_test_server_.lock(); }

void start_server(bool use_quic) {
  Envoy::ExtensionRegistry::registerFactories();
  strong_test_server_ = std::make_shared<Envoy::TestServer>();
  weak_test_server_ = strong_test_server_;

  if (auto e = test_server()) {
    e->startTestServer(use_quic);
  }
}

void shutdown_server() {
  // Reset the primary handle to the test_server,
  // but retain it long enough to synchronously shutdown.
  auto e = strong_test_server_;
  strong_test_server_.reset();
  e->shutdownTestServer();
}

int get_server_port() {
  if (auto e = test_server()) {
    return e->getServerPort();
  }
  return -1; // failure
}
