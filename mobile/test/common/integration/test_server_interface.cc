#include "test/common/integration/test_server_interface.h"

// NOLINT(namespace-envoy)

static std::shared_ptr<Envoy::TestServer> strong_test_server_;
static std::weak_ptr<Envoy::TestServer> weak_test_server_;

static std::shared_ptr<Envoy::TestServer> test_server() { return weak_test_server_.lock(); }

void start_server(Envoy::TestServerType test_server_type) {
  strong_test_server_ = std::make_shared<Envoy::TestServer>();
  weak_test_server_ = strong_test_server_;

  if (auto server = test_server()) {
    server->start(test_server_type);
  }
}

void shutdown_server() {
  // Reset the primary handle to the test_server,
  // but retain it long enough to synchronously shutdown.
  auto server = strong_test_server_;
  strong_test_server_.reset();
  server->shutdown();
}

int get_server_port() {
  if (auto server = test_server()) {
    return server->getPort();
  }
  return -1; // failure
}

void set_headers_and_data(absl::string_view header_key, absl::string_view header_value,
                          absl::string_view response_body) {
  if (auto server = test_server()) {
    // start_server() must be called before headers and data can be added.
    ASSERT(server);
    server->setHeadersAndData(header_key, header_value, response_body);
  }
}
