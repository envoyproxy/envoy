#pragma once

#include "test/integration/integration.h"

#include "gtest/gtest.h"

class Http2IntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_http2.json", {"echo", "http", "http_buffer"});
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
