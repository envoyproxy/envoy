#pragma once

#include "integration.h"

class Http2IntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    test_server_ = IntegrationTestServer::create("test/config/integration/server_http2.json");
    fake_upstreams_.emplace_back(new FakeUpstream(11000, FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(new FakeUpstream(11001, FakeHttpConnection::Type::HTTP1));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
