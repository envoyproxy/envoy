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
    createTestServer("test/config/integration/server.json",
                     {"http", "http_buffer", "tcp_proxy", "rds"});
  }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
} // Envoy
