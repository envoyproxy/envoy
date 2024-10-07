#include "test/common/integration/test_server_interface.h"

// NOLINT(namespace-envoy)

static std::shared_ptr<Envoy::TestServer> strong_test_http_server_;
static std::weak_ptr<Envoy::TestServer> weak_test_http_server_;
static std::shared_ptr<Envoy::TestServer> strong_test_proxy_server_;
static std::weak_ptr<Envoy::TestServer> weak_test_proxy_server_;

static std::shared_ptr<Envoy::TestServer> test_http_server() {
  if (strong_test_http_server_ == nullptr) {
    return nullptr;
  }
  return weak_test_http_server_.lock();
}

static std::shared_ptr<Envoy::TestServer> test_proxy_server() {
  if (strong_test_proxy_server_ == nullptr) {
    return nullptr;
  }
  return weak_test_proxy_server_.lock();
}

void start_http_server(Envoy::TestServerType test_server_type) {
  strong_test_http_server_ = std::make_shared<Envoy::TestServer>();
  weak_test_http_server_ = strong_test_http_server_;

  ASSERT(test_server_type == Envoy::TestServerType::HTTP1_WITHOUT_TLS ||
             test_server_type == Envoy::TestServerType::HTTP1_WITH_TLS ||
             test_server_type == Envoy::TestServerType::HTTP2_WITH_TLS ||
             test_server_type == Envoy::TestServerType::HTTP3,
         "Cannot start a proxy server with start_http_server. Use start_proxy_server instead.");
  if (auto server = test_http_server()) {
    server->start(test_server_type, 0);
  }
}

void start_proxy_server(Envoy::TestServerType test_server_type) {
  strong_test_proxy_server_ = std::make_shared<Envoy::TestServer>();
  weak_test_proxy_server_ = strong_test_proxy_server_;

  ASSERT(test_server_type == Envoy::TestServerType::HTTP_PROXY ||
             test_server_type == Envoy::TestServerType::HTTPS_PROXY,
         "Cannot start a HTTP server with start_proxy_server. Use start_http_server instead.");
  if (auto server = test_proxy_server()) {
    server->start(test_server_type, 0);
  }
}

void shutdown_http_server() {
  if (strong_test_http_server_ == nullptr) {
    return;
  }
  // Reset the primary handle to the test_http_server,
  // but retain it long enough to synchronously shutdown.
  auto server = strong_test_http_server_;
  strong_test_http_server_.reset();
  server->shutdown();
}

void shutdown_proxy_server() {
  if (strong_test_proxy_server_ == nullptr) {
    return;
  }
  // Reset the primary handle to the test_proxy_server,
  // but retain it long enough to synchronously shutdown.
  auto server = strong_test_proxy_server_;
  strong_test_proxy_server_.reset();
  server->shutdown();
}

int get_http_server_port() {
  if (auto server = test_http_server()) {
    return server->getPort();
  }
  return -1; // failure
}

int get_proxy_server_port() {
  if (auto server = test_proxy_server()) {
    return server->getPort();
  }
  return -1; // failure
}

void set_http_headers_and_data(absl::string_view header_key, absl::string_view header_value,
                               absl::string_view response_body) {
  if (auto server = test_http_server()) {
    // start_http_server() must be called before headers and data can be added.
    ASSERT(server);
    server->setHeadersAndData(header_key, header_value, response_body);
  }
}
