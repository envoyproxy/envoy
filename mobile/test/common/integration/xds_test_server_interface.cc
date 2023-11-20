#include "test/common/integration/xds_test_server_interface.h"

#include "test/common/integration/xds_test_server.h"

#include "extension_registry.h"

// NOLINT(namespace-envoy)

static std::shared_ptr<Envoy::XdsTestServer> strong_test_server_;
static std::weak_ptr<Envoy::XdsTestServer> weak_test_server_;

static std::shared_ptr<Envoy::XdsTestServer> testServer() { return weak_test_server_.lock(); }

void initXdsServer() {
  Envoy::ExtensionRegistry::registerFactories();
  strong_test_server_ = std::make_shared<Envoy::XdsTestServer>();
  weak_test_server_ = strong_test_server_;
}

const char* getXdsServerHost() {
  if (auto server = testServer()) {
    const char* host = strdup(server->getHost().c_str());
    return host;
  }
  return ""; // failure
}

int getXdsServerPort() {
  if (auto server = testServer()) {
    return server->getPort();
  }
  return -1; // failure
}

void startXdsServer() {
  if (auto server = testServer()) {
    server->start();
  }
}

void sendDiscoveryResponse(const envoy::service::discovery::v3::DiscoveryResponse& response) {
  if (auto server = testServer()) {
    ASSERT(server);
    server->send(response);
  }
}

void shutdownXdsServer() {
  // Reset the primary handle to the test_server,
  // but retain it long enough to synchronously shutdown.
  auto server = strong_test_server_;
  strong_test_server_.reset();
  server->shutdown();
}
