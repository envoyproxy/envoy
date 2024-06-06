#include "test/common/integration/engine_with_test_server.h"

namespace Envoy {

EngineWithTestServer::EngineWithTestServer(
    Platform::EngineBuilder& engine_builder, TestServerType type,
    const absl::flat_hash_map<std::string, std::string>& headers, absl::string_view body,
    const absl::flat_hash_map<std::string, std::string>& trailers) {
  test_server_.start(type);
  test_server_.setResponse(headers, body, trailers);
  engine_ = engine_builder.build();
}

EngineWithTestServer::~EngineWithTestServer() {
  // It is important that the we shutdown the TestServer first before terminating the Engine. This
  // is because when the Engine is terminated, the Logger::Context will be destroyed and
  // Logger::Context is a global variable that is used by both Engine and TestServer. By shutting
  // down the TestServer first, the TestServer will no longer access a Logger::Context that has been
  // destroyed.
  test_server_.shutdown();
  engine_->terminate();
}

Platform::EngineSharedPtr& EngineWithTestServer::engine() { return engine_; }

TestServer& EngineWithTestServer::testServer() { return test_server_; }

} // namespace Envoy
