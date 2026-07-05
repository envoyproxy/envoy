#pragma once

#ifndef TEST_CC_INTEGRATION_BASE_TEST_ENGINE_AND_SERVER_H_
#define TEST_CC_INTEGRATION_BASE_TEST_ENGINE_AND_SERVER_H_

#include <memory>
#include <string>

#include "test/cc/integration/base/test_engine_builder.h"
#include "library/cc/engine.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "test/common/integration/test_server.h"

namespace Envoy {

// Test only class.
class TestEngineAndServer {
public:
  TestEngineAndServer(Platform::TestEngineBuilder& engine_builder, TestServerType type,
                      const absl::flat_hash_map<std::string, std::string>& headers = {},
                      absl::string_view body = "",
                      const absl::flat_hash_map<std::string, std::string>& trailers = {});
  ~TestEngineAndServer();

  std::shared_ptr<Platform::Engine> engine();

  TestServer& test_server();

private:
  std::shared_ptr<Platform::Engine> engine_;
  TestServer test_server_;
};

} // namespace Envoy

#endif // TEST_CC_INTEGRATION_BASE_TEST_ENGINE_AND_SERVER_H_
