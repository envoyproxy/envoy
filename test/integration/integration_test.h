#pragma once

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class IntegrationTest : public BaseIntegrationTest,
                        public testing::TestWithParam<Network::Address::IpVersion> {
public:
  IntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer("test/config/integration/server.json", api_filesystem_config_,
                        {"http", "http_forward", "http_buffer", "rds"});
  }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  ApiFilesystemConfig api_filesystem_config_;
};
} // namespace Envoy
