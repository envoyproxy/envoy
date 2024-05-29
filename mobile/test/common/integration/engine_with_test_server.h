#pragma once

#include "test/common/integration/test_server.h"

#include "library/cc/engine_builder.h"

namespace Envoy {

/**
 * This class starts `TestServer` and build the `Engine` and ensures that the `TestServer` is
 * shutdown first before terminating the `Engine` in order to avoid accessing the destroyed
 * `Logger::Context` given that the `Logger::Context` is a global variable used by both the
 * `TestServer` and `Engine`.
 */
class EngineWithTestServer {
public:
  EngineWithTestServer(Platform::EngineBuilder& engine_builder, TestServerType type,
                       const absl::flat_hash_map<std::string, std::string>& headers = {},
                       absl::string_view body = "",
                       const absl::flat_hash_map<std::string, std::string>& trailers = {});
  ~EngineWithTestServer();

  /** Returns the reference of `Engine` created. */
  Platform::EngineSharedPtr& engine();

  /** Returns the reference of `TestServer` started. */
  TestServer& testServer();

private:
  Platform::EngineSharedPtr engine_;
  TestServer test_server_;
};

} // namespace Envoy
