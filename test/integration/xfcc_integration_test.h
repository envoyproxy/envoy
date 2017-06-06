#pragma once

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class XfccIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Initializer for an individual test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_http2_upstream.json",
                     {"http", "http_buffer", "http1_buffer"});
  }

  /**
   * Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

private:
  const std::string xfcc_header_ = "BY=test://bar.com/client;Hash=123456;SAN=test://foo.com/frontend";
};
} // Envoy
