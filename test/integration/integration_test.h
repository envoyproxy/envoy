#pragma once

#include "test/integration/integration.h"

#include "gtest/gtest.h"

class IntegrationTest : public BaseIntegrationTest,
                        public testing::TestWithParam<Network::Address::IpVersion> {
public:
  /**
   * Initializer for individual integration tests.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server.json", GetParam(),
                     {"echo", "http", "http_buffer", "tcp_proxy", "rds"});
  }

  /**
   *  Destructor for individual integration tests.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
