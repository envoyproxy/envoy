#pragma once

#include "test/integration/integration.h"

class Http2UpstreamIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    test_server_ =
        IntegrationTestServer::create("test/config/integration/server_http2_upstream.json");
    fake_upstreams_.emplace_back(new FakeUpstream(11000, FakeHttpConnection::Type::HTTP2));
    fake_upstreams_.emplace_back(new FakeUpstream(11001, FakeHttpConnection::Type::HTTP2));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
